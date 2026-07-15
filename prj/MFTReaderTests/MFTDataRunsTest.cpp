
#include "gtest/gtest.h"
#include "Readers.h"
#include "TestUtils.h"
#include "MFTBaseParamTest.h"



class MFTDataRunDecodeTest : public MFTStringParamTest
{
public:
    static void SetUpTestCase()
    {
        FName = "MFTDataRunDecodeParamTest";
        MFTStringParamTest::SetUpTestCase();
    }

    void SetUp() override
    {
        // it is important to call SetUp of the parent class here 
        MFTStringParamTest::SetUp();

        // add here your test initialization code
    }

    // You can implement all the usual fixture class members here.
    // To access the test parameter, call GetParam() from class TestWithParam<T>.
};


// Inside a test, access the test parameter with the GetParam() method of the TestWithParam<T> class:
TEST_P(MFTDataRunDecodeTest, DecodeDataRuns_1)
{
    TMFTRecordLoader ldr;
    TMFTParserBase parser(ldr);

    string_t fileName = GetParam();
    std::ifstream file(fileName);
    if (!file) FAIL() << "Error opening file '" << fileName << "'";

    std::vector<uint8_t> numbers;
    std::string line;

    while (std::getline(file, line))
    {
        std::istringstream iss(line);
        uint32_t num;

        numbers.clear();

        while (iss >> std::hex >> num) {
            ASSERT_LT(num, 256U);
            numbers.push_back((uint8_t)num);
        }

        uint32_t sz = (uint32_t)numbers.size() * sizeof(numbers[0]);
        uint8_t* buf = (uint8_t*)calloc(sizeof(MFT_ATTR_HEADER) + sz, 1);
        ASSERT_NE(buf, nullptr);
        uint8_t* runsBuf = buf + sizeof(MFT_ATTR_HEADER);
        memcpy_s(runsBuf, sz, numbers.data(), sz);

        MFT_ATTR_HEADER* attr = (MFT_ATTR_HEADER*)buf;
        attr->AttrType = ATTR_DATA;
        attr->AttrSize = sz + sizeof(MFT_ATTR_HEADER);
        attr->NonResidentFlag = 1;
        attr->nonres.DataRunsOffset = sizeof(MFT_ATTR_HEADER);
        attr->nonres.StartVCN = 0;
        attr->nonres.CompressionUnitSize = 0;
        attr->nonres.StreamSize = 1024 * 1024;
        attr->nonres.RealSize = 1024 * 1024;
        attr->nonres.AllocatedSize = 1024 * 1024;

        TDataRuns runs;
        bool res = parser.DecodeDataRuns(attr, runs);
        ASSERT_TRUE(res);

        free(buf);

        if (!std::getline(file, line)) FAIL() << "Error: Incorrect file";

        // check decoded data runs with etalon data

        std::istringstream iss2(line);
        iss2 >> std::hex >> num; // number of data runs
        ASSERT_EQ(num, runs.Count());

        DATA_RUN_ITEM rli;
        for (uint i = 0; i < num; i++)
        {
            uint32_t lcn, len;
            rli = runs[i];
            iss2 >> lcn;
            iss2 >> len;
            ASSERT_EQ(lcn, rli.lcn);
            ASSERT_EQ(len, rli.len);
        }

        ASSERT_FALSE(iss2 >> num); // check that we've read entire line

    }

    file.close();
};

INSTANTIATE_TEST_CASE_P(DecodeDataRuns1, MFTDataRunDecodeTest, testing::Values(_T(TEST_DATA_DIR "DataRuns1.txt")));
INSTANTIATE_TEST_CASE_P(DecodeDataRuns2, MFTDataRunDecodeTest, testing::Values(_T(TEST_DATA_DIR "DataRuns2.txt")));

