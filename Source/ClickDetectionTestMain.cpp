#include <JuceHeader.h>
#include <iostream>

class ConsoleUnitTestRunner : public juce::UnitTestRunner
{
public:
    void logMessage(const juce::String& message) override
    {
        std::cout << message << std::endl;
    }
};

int main()
{
    ConsoleUnitTestRunner runner;
    runner.setAssertOnFailure(false);
    runner.runTestsInCategory("Click Detection");

    int failures = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
        if (const auto* result = runner.getResult(i))
            failures += result->failures;

    return failures == 0 ? 0 : 1;
}
