#include "gtest/gtest.h"
#include <vector>
#include "logengine2/dynamicarrays.h"
#include "strutils/include/string_utils.h"

class StringToArrayParamTest : public testing::TestWithParam<std::tuple<std::string, char, int, std::string> > 
{
protected:
    std::vector<std::string> arr;
    void SetUp() override
    {
       // arr.Clear();
    }

    // You can implement all the usual fixture class members here.
    // To access the test parameter, call GetParam() from class
    // TestWithParam<T>.

};

// put all array items into a single sting one-by-one without any delimiters
template<typename STRING>
STRING toString(const std::vector<STRING>& array)
{
    // make sure that STRING is one of instantiations of std::basic_string
    static_assert(std::is_base_of<std::basic_string<typename STRING::value_type, typename STRING::traits_type>, STRING>::value);

    std::string res;
    res.reserve(100ull * array.size()); // to reduce number of memory re-allocations we assume that each string in the array has 100 characters

    for (uint i = 0; i < array.size(); i++)
    {
        res.append(array[i]);
    }

    return res;
}

TEST_P(StringToArrayParamTest, StringToArrayTest_1) // Inside a test, access the test parameter with the GetParam() method of the TestWithParam<T> class:
{
    std::string initial, result;
    char delim;
    uint32_t cnt;

    std::tie(initial, delim, cnt, result) = GetParam();

    StringToArray(initial, arr, delim);

    std::string res = toString(arr);
    EXPECT_EQ(arr.size(), cnt);
    EXPECT_EQ(res, result);
};


INSTANTIATE_TEST_CASE_P(SimpleStrToArr1, StringToArrayParamTest, testing::Values(std::make_tuple("", '\n', 0, "")));
INSTANTIATE_TEST_CASE_P(SimpleStrToArr2, StringToArrayParamTest, testing::Values(std::make_tuple("\n", '\n', 0, "")));
INSTANTIATE_TEST_CASE_P(SimpleStrToArr3, StringToArrayParamTest, testing::Values(std::make_tuple(" ", '\n', 1, " ")));
INSTANTIATE_TEST_CASE_P(SimpleStrToArr4, StringToArrayParamTest, testing::Values(std::make_tuple(" \n", '\n', 1, " ")));
INSTANTIATE_TEST_CASE_P(SimpleStrToArr5, StringToArrayParamTest, testing::Values(std::make_tuple("\n ", '\n', 1, " ")));
INSTANTIATE_TEST_CASE_P(SimpleStrToArr6, StringToArrayParamTest, testing::Values(std::make_tuple(",,,,,", ',', 0, "")));
INSTANTIATE_TEST_CASE_P(SimpleStrToArr7, StringToArrayParamTest, testing::Values(std::make_tuple(" ,,,,, ", ',', 2, "  ")));
INSTANTIATE_TEST_CASE_P(SimpleStrToArr8, StringToArrayParamTest, testing::Values(std::make_tuple("1,2,3", ',', 3, "123")));
INSTANTIATE_TEST_CASE_P(SimpleStrToArr9, StringToArrayParamTest, testing::Values(std::make_tuple("1,,2,,3,", ',', 3, "123")));
INSTANTIATE_TEST_CASE_P(SimpleStrToArr10, StringToArrayParamTest, testing::Values(std::make_tuple(",1,2,,3", ',', 3, "123")));
INSTANTIATE_TEST_CASE_P(SimpleStrToArr11, StringToArrayParamTest, testing::Values(std::make_tuple(",,1,,2,3", ',', 3, "123")));

INSTANTIATE_TEST_CASE_P(SimpleStrToArr12, StringToArrayParamTest, testing::Values(std::make_tuple(",,,1,2,,,3,", ',', 3, "123")));
INSTANTIATE_TEST_CASE_P(SimpleStrToArr13, StringToArrayParamTest, testing::Values(std::make_tuple("1,2,,3,", ',', 3, "123")));
INSTANTIATE_TEST_CASE_P(SimpleStrToArr14, StringToArrayParamTest, testing::Values(std::make_tuple("1,,2,3,,,,", ',', 3, "123")));
INSTANTIATE_TEST_CASE_P(SimpleStrToArr15, StringToArrayParamTest, testing::Values(std::make_tuple("12,34,56", ',', 3, "123456")));
INSTANTIATE_TEST_CASE_P(SimpleStrToArr16, StringToArrayParamTest, testing::Values(std::make_tuple("1,,23,5,", ',', 3, "1235")));

INSTANTIATE_TEST_CASE_P(SimpleStrToArr17, StringToArrayParamTest, testing::Values(std::make_tuple(",123,4,55", ',', 3, "123455")));
INSTANTIATE_TEST_CASE_P(SimpleStrToArr18, StringToArrayParamTest, testing::Values(std::make_tuple("1,,23,5,", '2', 2, "1,,3,5,")));
INSTANTIATE_TEST_CASE_P(SimpleStrToArr19, StringToArrayParamTest, testing::Values(std::make_tuple("1,,23,5,", ' ', 1, "1,,23,5,")));
INSTANTIATE_TEST_CASE_P(SimpleStrToArr20, StringToArrayParamTest, testing::Values(std::make_tuple("1,,23,5", '5', 1, "1,,23,")));
INSTANTIATE_TEST_CASE_P(SimpleStrToArr21, StringToArrayParamTest, testing::Values(std::make_tuple("\n\n235", '\n', 1, "235")));
INSTANTIATE_TEST_CASE_P(SimpleStrToArr22, StringToArrayParamTest, testing::Values(std::make_tuple("1235\n\n", '\n', 1, "1235")));


class StringToArrayStrParamTest : public testing::TestWithParam<std::tuple<std::string, std::string, int, std::string> >
{
protected:
    std::vector<std::string> arr;
    void SetUp() override
    {
        // arr.Clear();
    }

    // You can implement all the usual fixture class members here.
    // To access the test parameter, call GetParam() from class
    // TestWithParam<T>.

};

TEST_P(StringToArrayStrParamTest, StringToArrayStrTest_1) // Inside a test, access the test parameter with the GetParam() method of the TestWithParam<T> class:
{
    std::string initial, result, delimstr;
    uint32_t cnt;

    std::tie(initial, delimstr, cnt, result) = GetParam();

    StringToArray(initial, arr, delimstr);

    std::string res = toString(arr);
    EXPECT_EQ(arr.size(), cnt);
    EXPECT_EQ(res, result);
};


INSTANTIATE_TEST_CASE_P(StringToArr1, StringToArrayStrParamTest, testing::Values(std::make_tuple("", "\n", 0, "")));
INSTANTIATE_TEST_CASE_P(StringToArr2, StringToArrayStrParamTest, testing::Values(std::make_tuple("\n", "\n", 0, "")));
INSTANTIATE_TEST_CASE_P(StringToArr3, StringToArrayStrParamTest, testing::Values(std::make_tuple(" ", "\n", 1, " ")));
INSTANTIATE_TEST_CASE_P(StringToArr4, StringToArrayStrParamTest, testing::Values(std::make_tuple(" \n", "\n", 1, " ")));
INSTANTIATE_TEST_CASE_P(StringToArr5, StringToArrayStrParamTest, testing::Values(std::make_tuple("\n ", "\n", 1, " ")));
INSTANTIATE_TEST_CASE_P(StringToArr6, StringToArrayStrParamTest, testing::Values(std::make_tuple(",,,,,", ",", 0, "")));
INSTANTIATE_TEST_CASE_P(StringToArr7, StringToArrayStrParamTest, testing::Values(std::make_tuple(" ,,,,, ", ",", 2, "  ")));
INSTANTIATE_TEST_CASE_P(StringToArr8, StringToArrayStrParamTest, testing::Values(std::make_tuple("1,2,3", ",", 3, "123")));
INSTANTIATE_TEST_CASE_P(StringToArr9, StringToArrayStrParamTest, testing::Values(std::make_tuple("1,,2,,3,", ",", 3, "123")));
INSTANTIATE_TEST_CASE_P(StringToArr10, StringToArrayStrParamTest, testing::Values(std::make_tuple(",1,2,,3", ",", 3, "123")));
INSTANTIATE_TEST_CASE_P(StringToArr11, StringToArrayStrParamTest, testing::Values(std::make_tuple(",,1,,2,3", ",", 3, "123")));

INSTANTIATE_TEST_CASE_P(StringToArr12, StringToArrayStrParamTest, testing::Values(std::make_tuple(",,,1,2,,,3,", ",", 3, "123")));
INSTANTIATE_TEST_CASE_P(StringToArr13, StringToArrayStrParamTest, testing::Values(std::make_tuple("1,2,,3,", ",", 3, "123")));
INSTANTIATE_TEST_CASE_P(StringToArr14, StringToArrayStrParamTest, testing::Values(std::make_tuple("1,,2,3,,,,", ",", 3, "123")));
INSTANTIATE_TEST_CASE_P(StringToArr15, StringToArrayStrParamTest, testing::Values(std::make_tuple("12,34,56", ",", 3, "123456")));
INSTANTIATE_TEST_CASE_P(StringToArr16, StringToArrayStrParamTest, testing::Values(std::make_tuple("1,,23,5,", ",", 3, "1235")));

INSTANTIATE_TEST_CASE_P(StringToArr17, StringToArrayStrParamTest, testing::Values(std::make_tuple(",123,4,55", ",", 3, "123455")));
INSTANTIATE_TEST_CASE_P(StringToArr18, StringToArrayStrParamTest, testing::Values(std::make_tuple("1,,23,5,", "2", 2, "1,,3,5,")));
INSTANTIATE_TEST_CASE_P(StringToArr19, StringToArrayStrParamTest, testing::Values(std::make_tuple("1,,23,5,", " ", 1, "1,,23,5,")));
INSTANTIATE_TEST_CASE_P(StringToArr20, StringToArrayStrParamTest, testing::Values(std::make_tuple("1,,23,5", "5", 1, "1,,23,")));
INSTANTIATE_TEST_CASE_P(StringToArr21, StringToArrayStrParamTest, testing::Values(std::make_tuple("\n\n235", "\n", 1, "235")));
INSTANTIATE_TEST_CASE_P(StringToArr22, StringToArrayStrParamTest, testing::Values(std::make_tuple("1235\n\n", "\n", 1, "1235")));

INSTANTIATE_TEST_CASE_P(StringToArr23, StringToArrayStrParamTest, testing::Values(std::make_tuple("", " \n", 0, "")));
INSTANTIATE_TEST_CASE_P(StringToArr24, StringToArrayStrParamTest, testing::Values(std::make_tuple("\n", "\n,", 0, "")));
INSTANTIATE_TEST_CASE_P(StringToArr25, StringToArrayStrParamTest, testing::Values(std::make_tuple(" ", "a\n,", 1, " ")));
INSTANTIATE_TEST_CASE_P(StringToArr26, StringToArrayStrParamTest, testing::Values(std::make_tuple(" \n", ";f\n", 1, " ")));
INSTANTIATE_TEST_CASE_P(StringToArr27, StringToArrayStrParamTest, testing::Values(std::make_tuple("\n ", "\n0", 1, " ")));
INSTANTIATE_TEST_CASE_P(StringToArr28, StringToArrayStrParamTest, testing::Values(std::make_tuple(",,,,,", ",,;", 0, "")));
INSTANTIATE_TEST_CASE_P(StringToArr29, StringToArrayStrParamTest, testing::Values(std::make_tuple(" ,,,,, ", ",.\n", 2, "  ")));
INSTANTIATE_TEST_CASE_P(StringToArr30, StringToArrayStrParamTest, testing::Values(std::make_tuple("1,2,3", ",4 ", 3, "123")));
INSTANTIATE_TEST_CASE_P(StringToArr31, StringToArrayStrParamTest, testing::Values(std::make_tuple("1,,2,,3,", ",567890.", 3, "123")));
INSTANTIATE_TEST_CASE_P(StringToArr32, StringToArrayStrParamTest, testing::Values(std::make_tuple(",1,2,,3", ":,;", 3, "123")));
INSTANTIATE_TEST_CASE_P(StringToArr33, StringToArrayStrParamTest, testing::Values(std::make_tuple(",,1,,2,3", ",.4", 3, "123")));

INSTANTIATE_TEST_CASE_P(StringToArr34, StringToArrayStrParamTest, testing::Values(std::make_tuple(",,,1,2,,,3,", "0,0", 3, "123")));
INSTANTIATE_TEST_CASE_P(StringToArr35, StringToArrayStrParamTest, testing::Values(std::make_tuple("1,2,,3,", ";,:.", 3, "123")));
INSTANTIATE_TEST_CASE_P(StringToArr36, StringToArrayStrParamTest, testing::Values(std::make_tuple("1,,2,3,,,,", "^%$,", 3, "123")));
INSTANTIATE_TEST_CASE_P(StringToArr37, StringToArrayStrParamTest, testing::Values(std::make_tuple("12,34,56", "(,)*", 3, "123456")));
INSTANTIATE_TEST_CASE_P(StringToArr38, StringToArrayStrParamTest, testing::Values(std::make_tuple("1,,23,5,", ",,,,,", 3, "1235")));

INSTANTIATE_TEST_CASE_P(StringToArr39, StringToArrayStrParamTest, testing::Values(std::make_tuple(",123,4,55", " ,", 3, "123455")));
INSTANTIATE_TEST_CASE_P(StringToArr40, StringToArrayStrParamTest, testing::Values(std::make_tuple("1,,23,5,", " 2", 2, "1,,3,5,")));
INSTANTIATE_TEST_CASE_P(StringToArr41, StringToArrayStrParamTest, testing::Values(std::make_tuple("1,,23,5,", " .;", 1, "1,,23,5,")));
INSTANTIATE_TEST_CASE_P(StringToArr42, StringToArrayStrParamTest, testing::Values(std::make_tuple("1,,23,5", "54.0", 1, "1,,23,")));
INSTANTIATE_TEST_CASE_P(StringToArr43, StringToArrayStrParamTest, testing::Values(std::make_tuple("\n\n235", "\n 1", 1, "235")));
INSTANTIATE_TEST_CASE_P(StringToArr44, StringToArrayStrParamTest, testing::Values(std::make_tuple("1235\n\n", "4,\n", 1, "1235")));

