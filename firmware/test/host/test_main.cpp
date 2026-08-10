#include "check.hpp"

void run_log_tests();
void run_cli_tests();

int main() {
    run_log_tests();
    run_cli_tests();
    return check_summary("host");
}
