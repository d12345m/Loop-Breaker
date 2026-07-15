#include <JuceHeader.h>
#include <iostream>

class ConsoleUnitTestRunner : public juce::UnitTestRunner
{
public:
    void logMessage(const juce::String& message) override
    {
        std::cout << message << std::endl;
        if (message.containsIgnoreCase("FAILED"))
            ++failureLines;
    }

    int failureLines = 0;
};

int main()
{
    ConsoleUnitTestRunner runner;
    runner.setAssertOnFailure(false);
    runner.runTestsInCategory("Click Detection");
    return runner.failureLines == 0 ? 0 : 1;
}
