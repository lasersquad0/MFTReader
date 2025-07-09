#pragma once
//
// Project includes
//
#include "OptionsList.h"
#include "CommandLine.h"

//
// Libs include
//

//
// STDLib/OS includes
//
#include <vector>
#include <string>

///////////////////////////////////////////////////////////////////////////////

/**
 * @brief This class iterates over all the program arguments. Parse all the
 * options and the option arguments and store it into CCommandLine instance.
 */
class CDefaultParser
{
public:
    /**
     * @brief Get the last error during the command line arguments parsing.
     * @return A string with the error.
     */
    inline const cli_string &GetLastError() { return m_LastError; };

    /**
     * @brief Parse the program command line arguments. An option stars
     * with one to two dashes. "-i" is an option short name, "--input" is
     * the option long name, an option is followed by the arguments if any.
     *
     * @param options A valid pointer to the option list instance.
     * @param cmd A valid pointer to the command line instance.
     * @param argv A valid pointer to the program arguments.
     * @param argc Number of the program arguments.
     * @return true on success, false otherwise.
     */
    bool Parse(COptionsList *options, CCommandLine *cmd, _TCHAR *argv[], int argc);

    /**
     * @brief Parse the program command line arguments. An option stars with
     * with one to two dashes. "-i" is an option short name, "--input" is
     * the option long name, an option is followed by the arguments if any.
     *
     * @param options A valid pointer to the option list instance.
     * @param cmd A valid pointer to the command line instance.
     * @param args A vector with the program arguments.
     * @return true on success, false otherwise.
     */
    bool Parse(COptionsList *options, CCommandLine *cmd, const vector_string_t& args);

private:
    /**
     * @brief Handle command line arguments. A token can be the option name or
     * the option argument. An option name starts with one or two dashes for
     * short or long name.
     *
     * @note Currently unknown token are ignored. In case an option receives
     * more arguments than expected an error will be raised. 
     * In case an option receives less arguments than required an error will be raised.
     *
     * @param token The command line argument.
     * @return true on success, false otherwise.
     */
    bool HandleToken(const cli_string &token);

    /**
     * @brief Handle long name tokens.
     * @param token Token name.
     */
    bool HandleLongOption(const cli_string &token);

    /**
     * @brief Handle short name token.
     * @param token Token name.
     */
    bool HandleShortOption(const cli_string &token);

    /**
     * @brief Updates the option data structure that handles options with
     * required arguments. If the option arguments are complete,
     * the option is removed from the data structure.
     *
     * @param option A valid pointer to the option.
     */
    void UpdateOptionsWithArguments(COption *option);

    /**
     * @brief Updates the data structure that handles the required options.
     * If the required option is processed it is removed from the data
     * structure.
     *
     * @param option A valid pointer to the option.
     */
    void UpdateRequiredOptions(COption *option);

    /**
     * @brief Checks for missing arguments required by the option.
     * @return true if there are missing arguments, false otherwise.
     */
    bool CheckMissingRequiredArguments();

    /**
     * @brief Check for missing required options.
     * @return true if there are missing options, false otherwise.
     */
    bool CheckMissingRequiredOptions();

    /**
    * @brief Check for missing required options.
    * @return true if there are missing options, false otherwise.
    */
    //bool CheckMissingArguments();

private:
    /** @brief List of accepted options. */
    COptionsList *m_OptionsList = nullptr;

    /** @brief Store the processed options */
    CCommandLine *m_CommandLine = nullptr;

    /** @brief A pointer to the current option being parsed. */
    COption *m_CurrentOption = nullptr;

    /** @brief Contains the last parsing error. */
    cli_string m_LastError;

    /** @brief List of required options. */
    vector_option_pt m_ExpectedOption;

    /** @brief List of option with required arguments. */
    vector_option_pt m_ExpectedOptionWithArguments;
};

