#include <vcclr.h>

#include <Python.h>

{% if cookiecutter.console_app %}
{% else %}
#include "CrashDialog.h"
{% endif %}
#include <fcntl.h>
#include <windows.h>

using namespace System;
using namespace System::Diagnostics;
using namespace System::IO;
using namespace System::Windows::Forms;
using namespace System::Globalization;  // for timestamp formatting


// A global indicator of the debug level
char *debug_mode;

#define info_log(...) printf(__VA_ARGS__)
#define debug_log(...) if (debug_mode) printf(__VA_ARGS__)

#define MAX_LOG_FILE_AGE_DAYS 7

wchar_t* wstr(String^);
void setup_stdout(FileVersionInfo^);
void crash_dialog(String^);
String^ format_traceback(PyObject *type, PyObject *value, PyObject *traceback);
String^ PyObject_CppString(PyObject *obj);

int Main(array<String^>^ args) {
    int ret = 0;
    size_t size;
    FileVersionInfo^ version_info;
    PyStatus status;
    PyConfig config;
    String^ python_home;
    String^ app_module_name;
    String^ path;
    String^ traceback_str;
    wchar_t *app_packages_path_str;
    wchar_t *app_module_str;
    PyObject *app_packages_path;
    PyObject *app_module;
    PyObject *module;
    PyObject *module_attr;
    PyObject *method_args;
    PyObject *result;
    PyObject *exc_type;
    PyObject *exc_value;
    PyObject *exc_traceback;
    PyObject *systemExit_code;

    // Set the global debug state based on the runtime environment
    _dupenv_s(&debug_mode, &size, "BRIEFCASE_DEBUG");

    // Uninitialize the Windows threading model; allow apps to make
    // their own threading model decisions.
    CoUninitialize();

    // Get details of the app from app metadata
    version_info = FileVersionInfo::GetVersionInfo(Application::ExecutablePath);

    // Set up stdout/err handling
    setup_stdout(version_info);

    // Preconfigure the Python interpreter;
    // This ensures the interpreter is in Isolated mode,
    // and is using UTF-8 encoding.
    debug_log("PreInitializing Python runtime...\n");
    PyPreConfig pre_config;
    PyPreConfig_InitPythonConfig(&pre_config);
    pre_config.utf8_mode = 1;
    pre_config.isolated = 1;
    status = Py_PreInitialize(&pre_config);
    if (PyStatus_Exception(status)) {
        crash_dialog("Unable to pre-initialize Python runtime.");
        Py_ExitStatusException(status);
    }

    // Pre-initialize Python configuration
    PyConfig_InitIsolatedConfig(&config);

    // Configure the Python interpreter:
    // Don't buffer stdio. We want output to appears in the log immediately
    config.buffered_stdio = 0;
    // Don't write bytecode; we can't modify the app bundle
    // after it has been signed.
    config.write_bytecode = 0;
    // Isolated apps need to set the full PYTHONPATH manually.
    config.module_search_paths_set = 1;

    // Set the home for the Python interpreter
    python_home = Application::StartupPath;
    debug_log("PythonHome: %S\n", wstr(python_home));
    status = PyConfig_SetString(&config, &config.home, wstr(python_home));
    if (PyStatus_Exception(status)) {
        crash_dialog("Unable to set PYTHONHOME: " + gcnew String(status.err_msg));
        PyConfig_Clear(&config);
        Py_ExitStatusException(status);
    }

    // Determine the app module name. Look for the BRIEFCASE_MAIN_MODULE
    // environment variable first; if that exists, we're probably in test
    // mode. If it doesn't exist, fall back to the MainModule key in the
    // main bundle.
    _wdupenv_s(&app_module_str, &size, L"BRIEFCASE_MAIN_MODULE");
    if (app_module_str) {
        app_module_name = gcnew String(app_module_str);
    } else {
        app_module_name = version_info->InternalName;
        app_module_str = wstr(app_module_name);
    }
    status = PyConfig_SetString(&config, &config.run_module, app_module_str);
    if (PyStatus_Exception(status)) {
        crash_dialog("Unable to set app module name: " + gcnew String(status.err_msg));
        PyConfig_Clear(&config);
        Py_ExitStatusException(status);
    }

    // Read the site config
    status = PyConfig_Read(&config);
    if (PyStatus_Exception(status)) {
        crash_dialog("Unable to read site config: " + gcnew String(status.err_msg));
        PyConfig_Clear(&config);
        Py_ExitStatusException(status);
    }

    // Set the full module path. This includes the stdlib, site-packages, and app code.
    debug_log("PYTHONPATH:\n");
    // The .zip form of the stdlib
    path = python_home + "\\python{{ cookiecutter.python_version|py_libtag }}.zip";
    debug_log("- %S\n", wstr(path));
    status = PyWideStringList_Append(&config.module_search_paths, wstr(path));
    if (PyStatus_Exception(status)) {
        crash_dialog("Unable to set .zip form of stdlib path: " + gcnew String(status.err_msg));
        PyConfig_Clear(&config);
        Py_ExitStatusException(status);
    }

    // The unpacked form of the stdlib
    path = python_home;
    debug_log("- %S\n", wstr(path));
    status = PyWideStringList_Append(&config.module_search_paths, wstr(path));
    if (PyStatus_Exception(status)) {
        crash_dialog("Unable to set unpacked form of stdlib path: " + gcnew String(status.err_msg));
        PyConfig_Clear(&config);
        Py_ExitStatusException(status);
    }

    // Add the app path
    path = System::Windows::Forms::Application::StartupPath + "\\app";
    debug_log("- %S\n", wstr(path));
    status = PyWideStringList_Append(&config.module_search_paths, wstr(path));
    if (PyStatus_Exception(status)) {
        crash_dialog("Unable to set app path: " + gcnew String(status.err_msg));
        PyConfig_Clear(&config);
        Py_ExitStatusException(status);
    }

    debug_log("Configure argc/argv...\n");
    wchar_t** argv = new wchar_t* [args->Length + 1];
    argv[0] = wstr(Application::ExecutablePath);
    for (int i = 0; i < args->Length; i++) {
        argv[i + 1] = wstr(args[i]);
    }
    status = PyConfig_SetArgv(&config, args->Length + 1, argv);
    if (PyStatus_Exception(status)) {
        crash_dialog("Unable to configure argc/argv: " + gcnew String(status.err_msg));
        PyConfig_Clear(&config);
        Py_ExitStatusException(status);
    }

    debug_log("Initializing Python runtime...\n");
    status = Py_InitializeFromConfig(&config);
    if (PyStatus_Exception(status)) {
        crash_dialog("Unable to initialize Python interpreter: " + gcnew String(status.err_msg));
        PyConfig_Clear(&config);
        Py_ExitStatusException(status);
    }

    try {
        // Adding the app_packages as site directory.
        //
        // This adds app_packages to sys.path and executes any .pth
        // files in that directory.
        path = System::Windows::Forms::Application::StartupPath + "\\app_packages";
        app_packages_path_str = wstr(path);

        debug_log("Adding app_packages as site directory: %S\n", app_packages_path_str);

        module = PyImport_ImportModule("site");
        if (module == NULL) {
            crash_dialog("Could not import site module");
            exit(-11);
        }

        module_attr = PyObject_GetAttrString(module, "addsitedir");
        if (module_attr == NULL || !PyCallable_Check(module_attr)) {
            crash_dialog("Could not access site.addsitedir");
            exit(-12);
        }

        app_packages_path = PyUnicode_FromWideChar(app_packages_path_str, wcslen(app_packages_path_str));
        if (app_packages_path == NULL) {
            crash_dialog("Could not convert app_packages path to unicode");
            exit(-13);
        }

        method_args = Py_BuildValue("(O)", app_packages_path);
        if (method_args == NULL) {
            crash_dialog("Could not create arguments for site.addsitedir");
            exit(-14);
        }

        result = PyObject_CallObject(module_attr, method_args);
        if (result == NULL) {
            crash_dialog("Could not add app_packages directory using site.addsitedir");
            exit(-15);
        }

        // Start the app module.
        //
        // From here to Py_ObjectCall(runmodule...) is effectively
        // a copy of Py_RunMain() (and, more  specifically, the
        // pymain_run_module() method); we need to re-implement it
        // because we need to be able to inspect the error state of
        // the interpreter, not just the return code of the module.
        debug_log("Running app module: %S\n", app_module_str);

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

        app_module = PyUnicode_FromWideChar(app_module_str, wcslen(app_module_str));
        if (app_module == NULL) {
            crash_dialog("Could not convert module name to unicode");
            exit(-3);
        }

        method_args = Py_BuildValue("(Oi)", app_module, 0);
        if (method_args == NULL) {
            crash_dialog("Could not create arguments for runpy._run_module_as_main");
            exit(-4);
        }

        // Print a separator to differentiate Python startup logs from app logs,
        // then flush stdout/stderr to ensure all startup logs have been output.
        debug_log("---------------------------------------------------------------------------\n");
        fflush(stdout);
        fflush(stderr);

        // Invoke the app module
        result = PyObject_Call(module_attr, method_args, NULL);

        if (result == NULL) {
            // Retrieve the current error state of the interpreter.
            PyErr_Fetch(&exc_type, &exc_value, &exc_traceback);
            PyErr_NormalizeException(&exc_type, &exc_value, &exc_traceback);

            if (exc_traceback == NULL) {
                crash_dialog("Could not retrieve traceback");
                exit(-5);
            }

            traceback_str = nullptr;
            if (PyErr_GivenExceptionMatches(exc_value, PyExc_SystemExit)) {
                systemExit_code = PyObject_GetAttrString(exc_value, "code");
                if (systemExit_code == NULL) {
                    traceback_str = "Could not determine exit code";
                    ret = -10;
                } else if (systemExit_code == Py_None) {
                    // SystemExit with a code of None; documented as a return
                    // code of 0.
                    ret = 0;
                } else if (PyLong_Check(systemExit_code)) {
                    // SystemExit with error code
                    ret = (int) PyLong_AsLong(systemExit_code);
                } else {
                    // Any other SystemExit value - convert to a string, and use
                    // the string as the traceback, and use the documented
                    // SystemExit return value of 1.
                    ret = 1;
                    traceback_str = PyObject_CppString(systemExit_code);
                }
            } else {
                // Non-SystemExit; likely an uncaught exception
                info_log("---------------------------------------------------------------------------\n");
                info_log("Application quit abnormally!\n");
                ret = -6;
                traceback_str = format_traceback(exc_type, exc_value, exc_traceback);
            }

            if (traceback_str != nullptr) {
                // Display stack trace in the crash dialog.
                crash_dialog(traceback_str);
            }
        }
    }
    catch (Exception^ exception) {
        crash_dialog("Python runtime error: " + exception);
        ret = -7;
    }
    Py_Finalize();

    return ret;
}

wchar_t *wstr(String^ str)
{
    pin_ptr<const wchar_t> pinned = PtrToStringChars(str);
    return (wchar_t*)pinned;
}

{% if cookiecutter.console_app %}
void setup_stdout(FileVersionInfo^ version_info) {
    // No special stdout handling required
}

void crash_dialog(System::String^ details) {
    // Write the error to the console
    printf("%S\n", wstr(details));
}
{% else %}
/**
 * Setup stdout and stderr to redirect to the console if it exists,
 * but output to a file it doesn't.
 */
void setup_stdout(FileVersionInfo^ version_info) {
    String^ log_folder;
    String^ src_log;
    FILE *log;

    // If we can attach to the console, then we're running in a terminal;
    // we can use stdout and stderr as normal. However, if there's no
    // console, we need to redirect stdout/err to a log file.
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
        log_folder = Environment::GetFolderPath(Environment::SpecialFolder::LocalApplicationData) + "\\" +
            version_info->CompanyName + "\\" +
            version_info->ProductName + "\\Logs";
        if (!Directory::Exists(log_folder)) {
            // If log folder doesn't exist, create it
            Directory::CreateDirectory(log_folder);
        } else {
            // Attempt to delete log files older than the configured time
            TimeSpan maxAge = TimeSpan::FromDays(MAX_LOG_FILE_AGE_DAYS);
            array<String^>^ logFiles = Directory::GetFiles(log_folder, version_info->InternalName + "_*.log");
            DateTime now = DateTime::Now;
            for each (String^ file in logFiles) {
                try {
                    DateTime creationTime = File::GetCreationTime(file);
                    if (now - creationTime > maxAge) {
                        File::Delete(file);
                    }
                }
                catch (Exception^ ex) {
                    // Log a message if the file could not be deleted
                    debug_log("Could not delete old log file: %S (%S)\n", wstr(file), wstr(ex->Message));
                }
            }
        }

        // Redirect stdout to a log file, in the
        // user's local Logs folder for the app.
        // stderr doesn't exist when running without an attached console;
        // sys.stderr will be None in the Python interpreter. This causes
        // all error output to be written to stdout.

        // Get current timestamp in format yyyyMMdd_HHmmss_fff (with milliseconds)
        String^ timestamp = DateTime::Now.ToString("yyyyMMdd_HHmmss_fff", CultureInfo::InvariantCulture);
        src_log = log_folder + "\\" + version_info->InternalName + "." + timestamp + ".log";
        _wfreopen_s(&log, wstr(src_log), L"w", stdout);
    }

    debug_log("Log started: %S\n", wstr(DateTime::Now.ToString("yyyy-MM-dd HH:mm:ssZ")));
}

/**
 * Construct and display a modal dialog to the user that contains
 * details of an error during application execution (usually a traceback).
 */
void crash_dialog(System::String^ details) {
    wchar_t *app_module_str;
    size_t size;
    // Write the error to the log
    printf("%S\n", wstr(details));

    // If there's an app module override, we're running in test mode; don't show error dialogs
    _wdupenv_s(&app_module_str, &size, L"BRIEFCASE_MAIN_MODULE");
    if (app_module_str) {
        return;
    }
    Briefcase::CrashDialog^ form;

    form = gcnew Briefcase::CrashDialog(details);
    form->ShowDialog();
}
{% endif %}

/**
 * Convert a Python object into a C++ String^. It's easiest to do this by
 * using the Python API to encode to UTF-8, and then convert the UTF-8 byte
 * string into UTF-16 using Windows APIs.
 */
String^ PyObject_CppString(PyObject* obj) {
    Py_ssize_t size;
    System::Text::UTF8Encoding^ utf8 = gcnew System::Text::UTF8Encoding;

    const char* bytes = PyUnicode_AsUTF8AndSize(PyObject_Str(obj), &size);

    return gcnew String(utf8->GetString((Byte *)bytes, (int) size));
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
        printf("Could not import traceback.\n");
        return "Could not import traceback";
    }

    format_exception = PyObject_GetAttrString(traceback_module, "format_exception");
    if (format_exception && PyCallable_Check(format_exception)) {
        traceback_list = PyObject_CallFunctionObjArgs(format_exception, type, value, traceback, NULL);
    } else {
        printf("Could not find 'format_exception' in 'traceback' module.\n");
        return "Could not find 'format_exception' in 'traceback' module.";
    }
    if (traceback_list == NULL) {
        printf("Could not format traceback.\n");
        return "Could not format traceback.";
    }

    // Concatenate all the lines of the traceback into a single string
    traceback_unicode = PyUnicode_Join(PyUnicode_FromString(""), traceback_list);

    // Convert the Python Unicode string into a UTF-16 Windows String.
    traceback_str = PyObject_CppString(traceback_unicode);

    // Clean up the traceback string, removing references to the installed app location
    traceback_str = traceback_str->Replace(Application::StartupPath, "");

    return traceback_str;
}
