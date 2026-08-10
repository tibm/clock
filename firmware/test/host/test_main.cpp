#include "check.hpp"

void run_log_tests();
void run_cli_tests();
void run_sim_tests();

int main() {
    run_log_tests();
    run_cli_tests();
    run_sim_tests();
    return check_summary("host");
}
