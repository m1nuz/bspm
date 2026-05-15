export module greeting;

export namespace greeting {

const char* message() {
    return "Hello from shared library";
}

} // namespace greeting
