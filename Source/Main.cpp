/*
  ==============================================================================

    This file contains the basic startup code for a JUCE application.

  ==============================================================================
*/

#include <JuceHeader.h>
// Fallback: ensure unit tests compiled in Debug even if project file not yet regenerated with defines
#if !defined(JUCE_UNIT_TESTS) && (defined(JUCE_DEBUG) || defined(DEBUG))
 #define JUCE_UNIT_TESTS 1
#endif
#define LEGACY_MAIN_COMPONENT 0
#if LEGACY_MAIN_COMPONENT
 #include "MainComponent.h"          // Legacy test UI (gated)
#endif
#include "MainAppComponent.h"        // New evolving application UI

// Toggle to switch between legacy test UI and new app UI.
// Set to 1 to use new MainAppComponent, 0 to revert quickly if needed.
// New app UI is now the default; legacy component can be re-enabled by setting LEGACY_MAIN_COMPONENT to 1 above.

//==============================================================================
class BufferTestApplication  : public juce::JUCEApplication
{
public:
    //==============================================================================
    BufferTestApplication() {}

    const juce::String getApplicationName() override       { return ProjectInfo::projectName; }
    const juce::String getApplicationVersion() override    { return ProjectInfo::versionString; }
    bool moreThanOneInstanceAllowed() override             { return true; }

    //==============================================================================
    void initialise (const juce::String& commandLine) override
    {
                // If launched with a test flag, run unit tests and exit (desktop only)
#if ! (JUCE_IOS || JUCE_ANDROID)
                if (commandLine.contains("--run-tests"))
                {
                    #if JUCE_UNIT_TESTS
            testMode = true;
            juce::Logger::writeToLog("Running JUCE unit tests (graceful shutdown mode)...");
            struct CountingRunner : juce::UnitTestRunner {
                int failures = 0;
                void logMessage (const juce::String& m) override {
                    juce::UnitTestRunner::logMessage(m);
                    if (m.containsIgnoreCase("FAILED")) ++failures;
                }
            } runner;
            runner.setAssertOnFailure(false);
            runner.runAllTests();
            if (runner.failures > 0) {
                juce::Logger::writeToLog(juce::String(runner.failures) + " failure line(s) detected.");
                setApplicationReturnValue(1);
            } else {
                juce::Logger::writeToLog("All tests passed.");
                setApplicationReturnValue(0);
            }
            // Trigger standard JUCE shutdown path to allow leak detectors a clean teardown
            quit();
            return;
                    #else
            testMode = true;
            juce::Logger::writeToLog("--run-tests specified but JUCE_UNIT_TESTS not enabled in this build.");
            setApplicationReturnValue(2);
            quit();
            return;
                    #endif
                }
#endif // desktop only test harness
        mainWindow.reset (new MainWindow (getApplicationName()));
    }

    void shutdown() override
    {
        // Add your application's shutdown code here..

        mainWindow = nullptr; // (deletes our window)
    }

    //==============================================================================
    void systemRequestedQuit() override
    {
        // This is called when the app is being asked to quit: you can ignore this
        // request and let the app carry on running, or call quit() to allow the app to close.
        quit();
    }

    void anotherInstanceStarted (const juce::String& commandLine) override
    {
        // When another instance of the app is launched while this one is running,
        // this method is invoked, and the commandLine parameter tells you what
        // the other instance's command-line arguments were.
    }

    //==============================================================================
    /*
        This class implements the desktop window that contains an instance of
        our MainComponent class.
    */
    class MainWindow    : public juce::DocumentWindow
    {
    public:
        MainWindow (juce::String name)
            : DocumentWindow (name,
                              juce::Desktop::getInstance().getDefaultLookAndFeel()
                                                          .findColour (juce::ResizableWindow::backgroundColourId),
                              DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
           #if LEGACY_MAIN_COMPONENT
            setContentOwned (new MainComponent(), true);
           #else
            setContentOwned (new MainAppComponent(), true);
           #endif

           #if JUCE_IOS || JUCE_ANDROID
            setFullScreen (true);
           #else
            setResizable (true, true);
            centreWithSize (getWidth(), getHeight());
           #endif

            setVisible (true);
        }

        void closeButtonPressed() override
        {
            // This is called when the user tries to close this window. Here, we'll just
            // ask the app to quit when this happens, but you can change this to do
            // whatever you need.
            JUCEApplication::getInstance()->systemRequestedQuit();
        }

        /* Note: Be careful if you override any DocumentWindow methods - the base
           class uses a lot of them, so by overriding you might break its functionality.
           It's best to do all your work in your content component instead, but if
           you really have to override any DocumentWindow methods, make sure your
           subclass also calls the superclass's method.
        */

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

private:
    std::unique_ptr<MainWindow> mainWindow;
    bool testMode = false; // if true we avoid extra UI allocations after tests
};

//==============================================================================
// This macro generates the main() routine that launches the app.
START_JUCE_APPLICATION (BufferTestApplication)
