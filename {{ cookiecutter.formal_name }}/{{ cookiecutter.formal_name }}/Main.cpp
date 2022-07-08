#include <vcclr.h>

#include <Python.h>

#include "CrashDialog.h"

using namespace System;
using namespace System::Diagnostics;
using namespace System::Windows::Forms;

wchar_t* string_to_wchar_t(String^);
void crash_dialog(String^);
String^ format_traceback(PyObject *type, PyObject *value, PyObject *traceback);

//[STAThreadAttribute]
int Main(array<String^>^ args) {
    int ret = 0;
    PyStatus status;
    PyConfig config;
    String^ python_home;
    String^ app_module_name;
    String^ path;
    String^ traceback_str;
    //const char* nslog_script;
    PyObject *app_module;
    PyObject *module;
    PyObject *module_attr;
    PyObject *method_args;
    PyObject *result;
    PyObject *exc_type;
    PyObject *exc_value;
    PyObject *exc_traceback;
    PyObject *systemExit_code;

    PyConfig_InitIsolatedConfig(&config);

    // Configure the Python interpreter:
    // Run at optimization level 1
    // (remove assertions, set __debug__ to False)
    config.optimization_level = 1;
    // Don't buffer stdio. We want output to appears in the log immediately
    config.buffered_stdio = 0;
    // Don't write bytecode; we can't modify the app bundle
    // after it has been signed.
    config.write_bytecode = 0;
    // Isolated apps need to set the full PYTHONPATH manually.
    config.module_search_paths_set = 1;

    // Set the home for the Python interpreter
    python_home = Application::StartupPath + "\\python";
    Console::WriteLine("PythonHome: " + python_home);
    status = PyConfig_SetString(&config, &config.home, string_to_wchar_t(python_home));
    if (PyStatus_Exception(status)) {
        crash_dialog("Unable to set PYTHONHOME: " + gcnew String(status.err_msg));
        PyConfig_Clear(&config);
        Py_ExitStatusException(status);
    }

    // Determine the app module name
    FileVersionInfo^ version_info = FileVersionInfo::GetVersionInfo(Application::ExecutablePath);
    app_module_name = version_info->InternalName;
    status = PyConfig_SetString(&config, &config.run_module, string_to_wchar_t(app_module_name));
    if (PyStatus_Exception(status)) {
        crash_dialog("Unable to set app module name: " + gcnew String(status.err_msg));
        PyConfig_Clear(&config);
        Py_ExitStatusException(status);
    }

    crash_dialog(
        "Module name:" + app_module_name + "\n" +
        "Startup path:" + Application::StartupPath + "\n" +
        "Executable path:" + Application::ExecutablePath + "\n" +
        "app data:" + Environment::GetFolderPath(Environment::SpecialFolder::ApplicationData) + "\n" +
        "local app data:" + Environment::GetFolderPath(Environment::SpecialFolder::LocalApplicationData) + "\n" +
        "user profile:" + Environment::GetFolderPath(Environment::SpecialFolder::UserProfile) + "\n"
    );

    // Read the site config
    status = PyConfig_Read(&config);
    if (PyStatus_Exception(status)) {
        crash_dialog("Unable to read site config: " + gcnew String(status.err_msg));
        PyConfig_Clear(&config);
        Py_ExitStatusException(status);
    }

    // Set the full module path. This includes the stdlib, site-packages, and app code.
    Console::WriteLine("PYTHONPATH:");
    // The .zip form of the stdlib
    path = python_home + "\\python310.zip";
    Console::WriteLine("- " + path);
    status = PyWideStringList_Append(&config.module_search_paths, string_to_wchar_t(path));
    if (PyStatus_Exception(status)) {
        crash_dialog("Unable to set .zip form of stdlib path: " + gcnew String(status.err_msg));
        PyConfig_Clear(&config);
        Py_ExitStatusException(status);
    }

    // The unpacked form of the stdlib
    path = python_home;
    // Console::WriteLine("- " + path);
    status = PyWideStringList_Append(&config.module_search_paths, string_to_wchar_t(path));
    if (PyStatus_Exception(status)) {
        crash_dialog("Unable to set unpacked form of stdlib path: " + gcnew String(status.err_msg));
        PyConfig_Clear(&config);
        Py_ExitStatusException(status);
    }

    // Add the app_packages path
    path = System::Windows::Forms::Application::StartupPath + "\\app_packages";
    Console::WriteLine("- " + path);
    status = PyWideStringList_Append(&config.module_search_paths, string_to_wchar_t(path));
    if (PyStatus_Exception(status)) {
        crash_dialog("Unable to set app packages path: " + gcnew String(status.err_msg));
        PyConfig_Clear(&config);
        Py_ExitStatusException(status);
    }

    // Add the app path
    path = System::Windows::Forms::Application::StartupPath + "\\app";
    Console::WriteLine("- " + path);
    status = PyWideStringList_Append(&config.module_search_paths, string_to_wchar_t(path));
    if (PyStatus_Exception(status)) {
        crash_dialog("Unable to set app path: " + gcnew String(status.err_msg));
        PyConfig_Clear(&config);
        Py_ExitStatusException(status);
    }

    Console::WriteLine("Configure argc/argv...");
    wchar_t** argv = new wchar_t* [args->Length];
    for (int i = 0; i < args->Length; i++) {
        argv[i] = string_to_wchar_t(args[i]);
    }
    status = PyConfig_SetArgv(&config, args->Length, argv);
    if (PyStatus_Exception(status)) {
        crash_dialog("Unable to configured argc/argv: " + gcnew String(status.err_msg));
        PyConfig_Clear(&config);
        Py_ExitStatusException(status);
    }

    Console::WriteLine("Initializing Python runtime...");
    status = Py_InitializeFromConfig(&config);
    if (PyStatus_Exception(status)) {
        crash_dialog("Unable to initialize Python interpreter: " + gcnew String(status.err_msg));
        PyConfig_Clear(&config);
        Py_ExitStatusException(status);
    }

    try {
        // // Install the nslog script to redirect stdout/stderr if available.
        // // Set the name of the python NSLog bootstrap script
        // nslog_script = [
        //     [[NSBundle mainBundle] pathForResource:@"app_packages/nslog"
        //                                     ofType:@"py"] cStringUsingEncoding:NSUTF8StringEncoding];

        // if (nslog_script == NULL) {
        //     NSLog(@"No Python NSLog handler found. stdout/stderr will not be captured.");
        //     NSLog(@"To capture stdout/stderr, add 'std-nslog' to your app dependencies.");
        // } else {
        //     NSLog(@"Installing Python NSLog handler...");
        //     FILE *fd = fopen(nslog_script, "r");
        //     if (fd == NULL) {
        //         crash_dialog(@"Unable to open nslog.py");
        //         exit(-1);
        //     }

        //     ret = PyRun_SimpleFileEx(fd, nslog_script, 1);
        //     fclose(fd);
        //     if (ret != 0) {
        //         crash_dialog(@"Unable to install Python NSLog handler");
        //         exit(ret);
        //     }
        // }

        // Start the app module.
        //
        // From here to Py_ObjectCall(runmodule...) is effectively
        // a copy of Py_RunMain() (and, more  specifically, the
        // pymain_run_module() method); we need to re-implement it
        // because we need to be able to inspect the error state of
        // the interpreter, not just the return code of the module.
        Console::WriteLine("Running app module: ", app_module_name);
        module = PyImport_ImportModule("runpy");
        if (module == NULL) {
            crash_dialog("Could not import runpy module");
            exit(-2);
        }

        module_attr = PyObject_GetAttrString(module, "_run_module_as_main");
        if (module_attr == NULL) {
            crash_dialog("Could not access runpy._run_module_as_main");
            exit(-3);
        }

        app_module = PyUnicode_FromWideChar(string_to_wchar_t(app_module_name), app_module_name->Length);
        if (app_module == NULL) {
            crash_dialog("Could not convert module name to unicode");
            exit(-3);
        }

        method_args = Py_BuildValue("(Oi)", app_module, 0);
        if (method_args == NULL) {
            crash_dialog("Could not create arguments for runpy._run_module_as_main");
            exit(-4);
        }

        result = PyObject_Call(module_attr, method_args, NULL);

        if (result == NULL) {
            // Retrieve the current error state of the interpreter.
            PyErr_Fetch(&exc_type, &exc_value, &exc_traceback);
            PyErr_NormalizeException(&exc_type, &exc_value, &exc_traceback);

            if (exc_traceback == NULL) {
                crash_dialog("Could not retrieve traceback");
                exit(-5);
            }

            if (PyErr_GivenExceptionMatches(exc_value, PyExc_SystemExit)) {
                systemExit_code = PyObject_GetAttrString(exc_value, "code");
                if (systemExit_code == NULL) {
                    Console::WriteLine("Could not determine exit code");
                    ret = -10;
                }
                else {
                    ret = (int) PyLong_AsLong(systemExit_code);
                }
            } else {
                ret = -6;
            }

            if (ret != 0) {
                Console::WriteLine("Application quit abnormally (Exit code %d)!", ret);

                traceback_str = format_traceback(exc_type, exc_value, exc_traceback);

                // Restore the error state of the interpreter.
                PyErr_Restore(exc_type, exc_value, exc_traceback);

                // Print exception to stderr.
                // In case of SystemExit, this will call exit()
                PyErr_Print();

                // Display stack trace in the crash dialog.
                crash_dialog(traceback_str);
                exit(ret);
            }
        }
        else {
            crash_dialog("App module returned");
        }
    }
    catch (Exception^ exception) {
        crash_dialog("Python runtime error: " + exception);
        ret = -7;
    }

    Py_Finalize();

    return ret;
}

wchar_t *string_to_wchar_t(String^ str)
{
    pin_ptr<const wchar_t> pinned = PtrToStringChars(str);
    return (wchar_t*)pinned;
}

/**
 * Construct and display a modal dialog to the user that contains
 * details of an error during application execution (usually a traceback).
 */
void crash_dialog(System::String^ details) {
    // Write the error to the log
    Console::WriteLine(details);

    Briefcase::CrashDialog^ form;

    form = gcnew Briefcase::CrashDialog(details);
    form->ShowDialog();
}

/**
 * Convert a Python traceback object into a user-suitable string, stripping off
 * stack context that comes from this stub binary.
 *
 * If any error occurs processing the traceback, the error message returned
 * will describe the mode of failure.
 */
String^ format_traceback(PyObject *type, PyObject *value, PyObject *traceback) {
    String ^traceback_str;
    PyObject *traceback_list;
    PyObject *traceback_module;
    PyObject *format_exception;
    PyObject *traceback_unicode;
    PyObject *inner_traceback;

    // Drop the top two stack frames; these are internal
    // wrapper logic, and not in the control of the user.
    for (int i = 0; i < 2; i++) {
        inner_traceback = PyObject_GetAttrString(traceback, "tb_next");
        if (inner_traceback != NULL) {
            traceback = inner_traceback;
        }
    }

    // Format the traceback.
    traceback_module = PyImport_ImportModule("traceback");
    if (traceback_module == NULL) {
        Console::WriteLine("Could not import traceback");
        return "Could not import traceback";
    }

    format_exception = PyObject_GetAttrString(traceback_module, "format_exception");
    if (format_exception && PyCallable_Check(format_exception)) {
        traceback_list = PyObject_CallFunctionObjArgs(format_exception, type, value, traceback, NULL);
    } else {
        Console::WriteLine("Could not find 'format_exception' in 'traceback' module");
        return "Could not find 'format_exception' in 'traceback' module";
    }
    if (traceback_list == NULL) {
        Console::WriteLine("Could not format traceback");
        return "Could not format traceback";
    }

    traceback_unicode = PyUnicode_Join(PyUnicode_FromString(""), traceback_list);
    traceback_str = gcnew String(PyUnicode_AsUTF8(PyObject_Str(traceback_unicode)));

    // Clean up the traceback string, removing references to the installed app location
    traceback_str = traceback_str->Replace(Application::StartupPath, "");

    return traceback_str;
}
