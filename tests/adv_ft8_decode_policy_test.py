#!/usr/bin/env python3
"""Guard the ADV-only V2 decode scheduling policy without emulating FreeRTOS."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]


def code(path):
    text = (ROOT / path).read_text()
    return re.sub(r'/\*.*?\*/|//[^\n]*', '', text, flags=re.S)


def block(text, marker):
    start = text.index('{', text.index(marker))
    depth = 1
    for end in range(start + 1, len(text)):
        depth += (text[end] == '{') - (text[end] == '}')
        if depth == 0:
            return text[start + 1:end], text[end + 1:]
    raise AssertionError(f'unclosed block after {marker}')


class DecodePolicy(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = code('platform/adv/adv_ft8_decode.c')

    def test_static_task_resources_and_affinity(self):
        for name, value in [('CORE', 1), ('PRIORITY', 4), ('STACK_BYTES', 8192)]:
            self.assertRegex(self.source,
                             rf'(?m)^#define\s+ADV_FT8_DECODE_{name}\s+{value}u?\s*$')
        self.assertRegex(self.source,
                         r'static\s+alignas\(portBYTE_ALIGNMENT\)\s+StackType_t\s+'
                         r's_decode_stack\[ADV_FT8_DECODE_STACK_BYTES\s*/\s*sizeof\(StackType_t\)\]')
        self.assertIn('_Static_assert(sizeof(s_decode_stack) == ADV_FT8_DECODE_STACK_BYTES,',
                      self.source)
        self.assertRegex(self.source, r'static\s+StaticTask_t\s+s_decode_tcb\s*;')
        self.assertRegex(self.source,
                         r'xTaskCreateStaticPinnedToCore\(\s*decode_task,\s*"ft8_decode",\s*'
                         r'sizeof\(s_decode_stack\),\s*app,\s*ADV_FT8_DECODE_PRIORITY,\s*'
                         r's_decode_stack,\s*&s_decode_tcb,\s*ADV_FT8_DECODE_CORE\s*\)')

    def test_active_work_and_idle_wait(self):
        task, _ = block(self.source, 'static void decode_task(')
        loop, _ = block(task, 'while (!atomic_load_explicit(')
        self.assertIn('app_controller_decode_worker_step(app, &did_work)', loop)
        active, after = block(loop, 'if (did_work)')
        self.assertRegex(after, r'^\s*else\s*\{')
        idle, tail = block(after, 'else')
        self.assertEqual(re.sub(r'\s+', '', idle), 'vTaskDelay(1);')
        self.assertFalse(tail.strip(), 'active work must immediately recheck the worker loop')
        self.assertNotRegex(loop, r'\btaskYIELD\s*\(')
        self.assertNotRegex(active, r'\b(?:vTaskDelay|vTaskDelayUntil|vTaskSuspend|xSemaphoreTake)\s*\(')
        self.assertIn('memory_order_acquire', task)

    def test_about_identity(self):
        for path in ['apps/ft8/src/ui_shell/ui_shell.c', 'tests/ft8_ui_smoke.c']:
            self.assertEqual(re.findall(r'MiniFT8-V3\.\d+', code(path)), ['MiniFT8-V3.087'])


if __name__ == '__main__':
    unittest.main()
