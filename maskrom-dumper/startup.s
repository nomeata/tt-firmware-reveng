/* startup.s -- binary entry: the launcher calls the load address, which branches to main(). */
.global main
_reset_handler:
    b main;
