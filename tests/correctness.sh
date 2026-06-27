ERROR_LOG=correctness_errors.log

echo "===================================="
echo -e "\033[32mChord Correctness Test Suite\033[0m"
echo "===================================="

echo ""

declare -a tests=(
    "Single-node storage"
    "Two-node routing"
    "Multi-node routing"
    "Ownership verification"
    "Finger table verification"
    "Ring consistency"
)

declare -a test_scripts=(
    "single_node"
    "two_node_routing"
    "multi_node_routing"
    "ownership_verification"
    "finger_table_verification"
    "ring_consistency"
)

>$ERROR_LOG
passed_tests=0

# Run each test script and capture the result
for i in "${!tests[@]}"; do
    test_name="${tests[$i]}"
    test_script="${test_scripts[$i]}"

    if ./correctness/${test_script}.sh > /dev/null 2>>$ERROR_LOG; then
        passed_tests=$((passed_tests + 1))
        printf "\e[32m[PASS]\033[0m"
    else
        printf "\e[31m[FAIL]\033[0m"
    fi
    printf " %s\n" "$test_name"
done

echo ""

RED="\033[31m"
GREEN="\033[32m"
COLOR=$RED
if [[ $passed_tests -eq ${#tests[@]} ]]; then
    COLOR=$GREEN
fi

echo -e "${COLOR}Passed $passed_tests / ${#tests[@]} tests\033[0m"