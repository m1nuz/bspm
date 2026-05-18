export module greeting;

#if defined(_WIN32)
#define BSPM_GREETING_API __declspec(dllexport)
#else
#define BSPM_GREETING_API
#endif

export namespace greeting {

BSPM_GREETING_API const char* message() {
    return "Hello from shared library";
}

} // namespace greeting
