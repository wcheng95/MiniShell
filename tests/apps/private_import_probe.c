/* Deliberately violate the runtime boundary without including private headers. */
extern void filesystem_handles_reset(void);

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    filesystem_handles_reset();
    return 0;
}
