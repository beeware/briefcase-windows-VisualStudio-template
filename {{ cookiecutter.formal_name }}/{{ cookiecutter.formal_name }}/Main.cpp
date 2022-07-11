#include <iostream>
#include <vcclr.h>

#include <Python.h>

#include "CrashDialog.h"

using namespace System;
using namespace System::Diagnostics;
using namespace System::IO;
using namespace System::Windows::Forms;

wchar_t* wstr(String^);
void crash_dialog(String^);
String^ format_traceback(PyObject *type, PyObject *value, PyObject *traceback);

//[STAThreadAttribute]
int Main(array<String^>^ args) {
    int ret = 0;
    FileVersionInfo^ version_info;
    String^ log_folder;
    String^ src_log;
    String^ dst_log;
    FILE* log;
    PyStatus status;
    PyConfig config;
    String^ python_home;
    String^ app_module_name;
    String^ path;
    String^ traceback_str;
    PyObject *app_module;
    PyObject *module;
    PyObject *module_attr;
    PyObject *method_args;
    PyObject *result;
    PyObject *exc_type;
    PyObject *exc_value;
    PyObject *exc_traceback;
    PyObject *systemExit_code;

    // Get details of the app from app metadata
    version_info = FileVersionInfo::GetVersionInfo(Application::ExecutablePath);

    // Redirect stdout to a log file in the user's local AppData.
    log_folder = Environment::GetFolderPath(Environment::SpecialFolder::LocalApplicationData) + "\\" +
        version_info->CompanyName + "\\" +
        version_info->ProductName + "\\Logs";
    if (!Directory::Exists(log_folder)) {
        // If log folder doesn't exist, create it
        Directory::CreateDirectory(log_folder);
    } else {
        // If it does, rotate the logs in that folder.
        // - Delete <app name>-9.log
        src_log = log_folder + "\\" + version_info->InternalName + "-9.log";
        if (File::Exists(src_log)) {
            File::Delete(src_log);
        }

        // - Move <app name>-8.log -> <app name>-9.log
        // - Move <app name>-7.log -> <app name>-8.log
        // - ...
        // - Move <app name>.log -> <app name>-2.log
        for (int dst_index = 9; dst_index >= 2; dst_index--) {
            if (dst_index == 2) {
                src_log = log_folder + "\\" + version_info->InternalName + ".log";
            } else {
                src_log = log_folder + "\\" + version_info->InternalName + "-" + (dst_index - 1) + ".log";
            }
            dst_log = log_folder + "\\" + version_info->InternalName + "-" + dst_index + ".log";

            if (File::Exists(src_log)) {
                File::Move(src_log, dst_log);
            }
        }
    }

    // Redirect stdout to a log file <app name>.log, in the
    // user's local Logs folder for the app.
    // stderr doesn't exist when running without an attached console;
    // sys.stderr will be None in the Python interpreter. This causes
    // all error output to be written to stdout.
    _wfreopen_s(&log, wstr(log_folder + "\\" + version_info->InternalName + ".log"), L"w", stdout);
    std::wcout << "Log started: " << wstr(DateTime::Now.ToString("yyyy-MM-dd HH:mm:ssZ")) << std::endl;

    // Preconfigure the Python interpreter;
    // This ensures the interpreter is in Isolated mode,
    // and is using UTF-8 encoding.
    std::wcout << "PreInitializing Python runtime..." << std::endl;
    PyPreConfig pre_config;
    PyPreConfig_InitPythonConfig(&pre_config);
    pre_config.utf8_mode = 1;
    pre_config.isolated = 1;
    status = Py_PreInitialize(&pre_config);
    if (PyStatus_Exception(status)) {
        crash_dialog("Unable to pre-initialize Python runtime.");
        fclose(log);
        Py_ExitStatusException(status);
    }

    // Pre-initialize Python configuration
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
    std::wcout << "PythonHome: " << wstr(python_home) << std::endl;
    status = PyConfig_SetString(&config, &config.home, wstr(python_home));
    if (PyStatus_Exception(status)) {
        crash_dialog("Unable to set PYTHONHOME: " + gcnew String(status.err_msg));
        PyConfig_Clear(&config);
        fclose(log);
        Py_ExitStatusException(status);
    }

    // Determine the app module name
    app_module_name = version_info->InternalName;
    status = PyConfig_SetString(&config, &config.run_module, wstr(app_module_name));
    if (PyStatus_Exception(status)) {
        crash_dialog("Unable to set app module name: " + gcnew String(status.err_msg));
        PyConfig_Clear(&config);
        fclose(log);
        Py_ExitStatusException(status);
    }

    // Read the site config
    status = PyConfig_Read(&config);
    if (PyStatus_Exception(status)) {
        crash_dialog("Unable to read site config: " + gcnew String(status.err_msg));
        PyConfig_Clear(&config);
        fclose(log);
        Py_ExitStatusException(status);
    }

    // Set the full module path. This includes the stdlib, site-packages, and app code.
    std::wcout << "PYTHONPATH:" << std::endl;
    // The .zip form of the stdlib
    path = python_home + "\\python310.zip";
    std::wcout << "- " << wstr(path) << std::endl;
    status = PyWideStringList_Append(&config.module_search_paths, wstr(path));
    if (PyStatus_Exception(status)) {
        crash_dialog("Unable to set .zip form of stdlib path: " + gcnew String(status.err_msg));
        PyConfig_Clear(&config);
        fclose(log);
        Py_ExitStatusException(status);
    }

    // The unpacked form of the stdlib
    path = python_home;
    std::wcout << "- " << wstr(path) << std::endl;
    status = PyWideStringList_Append(&config.module_search_paths, wstr(path));
    if (PyStatus_Exception(status)) {
        crash_dialog("Unable to set unpacked form of stdlib path: " + gcnew String(status.err_msg));
        PyConfig_Clear(&config);
        fclose(log);
        Py_ExitStatusException(status);
    }

    // Add the app_packages path
    path = System::Windows::Forms::Application::StartupPath + "\\app_packages";
    std::wcout << "- " << wstr(path) << std::endl;
    status = PyWideStringList_Append(&config.module_search_paths, wstr(path));
    if (PyStatus_Exception(status)) {
        crash_dialog("Unable to set app packages path: " + gcnew String(status.err_msg));
        PyConfig_Clear(&config);
        fclose(log);
        Py_ExitStatusException(status);
    }

    // Add the app path
    path = System::Windows::Forms::Application::StartupPath + "\\app";
    std::wcout << "- " << wstr(path) << std::endl;
    status = PyWideStringList_Append(&config.module_search_paths, wstr(path));
    if (PyStatus_Exception(status)) {
        crash_dialog("Unable to set app path: " + gcnew String(status.err_msg));
        PyConfig_Clear(&config);
        fclose(log);
        Py_ExitStatusException(status);
    }

    std::wcout << "Configure argc/argv..." << std::endl;
    wchar_t** argv = new wchar_t* [args->Length];
    for (int i = 0; i < args->Length; i++) {
        argv[i] = wstr(args[i]);
    }
    status = PyConfig_SetArgv(&config, args->Length, argv);
    if (PyStatus_Exception(status)) {
        crash_dialog("Unable to configure argc/argv: " + gcnew String(status.err_msg));
        PyConfig_Clear(&config);
        fclose(log);
        Py_ExitStatusException(status);
    }

    std::wcout << "Initializing Python runtime..." << std::endl;
    status = Py_InitializeFromConfig(&config);
    if (PyStatus_Exception(status)) {
        crash_dialog("Unable to initialize Python interpreter: " + gcnew String(status.err_msg));
        PyConfig_Clear(&config);
        fclose(log);
        Py_ExitStatusException(status);
    }

    // Initializing Python modifies stdout to be a UTF-8 stream.
    // Make sure std::wcout is configured the same.
    std::wcout.imbue(std::locale(".UTF8"));

    try {
        // Start the app module.
        //
        // From here to Py_ObjectCall(runmodule...) is effectively
        // a copy of Py_RunMain() (and, more  specifically, the
        // pymain_run_module() method); we need to re-implement it
        // because we need to be able to inspect the error state of
        // the interpreter, not just the return code of the module.
        std::wcout << "Running app module: " << wstr(app_module_name) << std::endl;

        module = PyImport_ImportModule("runpy");
        if (module == NULL) {
            crash_dialog("Could not import runpy module");
            fclose(log);
            exit(-2);
        }

        module_attr = PyObject_GetAttrString(module, "_run_module_as_main");
        if (module_attr == NULL) {
            crash_dialog("Could not access runpy._run_module_as_main");
            fclose(log);
            exit(-3);
        }

        app_module = PyUnicode_FromWideChar(wstr(app_module_name), app_module_name->Length);
        if (app_module == NULL) {
            crash_dialog("Could not convert module name to unicode");
            fclose(log);
            exit(-3);
        }

        method_args = Py_BuildValue("(Oi)", app_module, 0);
        if (method_args == NULL) {
            crash_dialog("Could not create arguments for runpy._run_module_as_main");
            fclose(log);
            exit(-4);
        }

        std::wcout << "===========================================================================" << std::endl;
        result = PyObject_Call(module_attr, method_args, NULL);

        if (result == NULL) {
            // Retrieve the current error state of the interpreter.
            PyErr_Fetch(&exc_type, &exc_value, &exc_traceback);
            PyErr_NormalizeException(&exc_type, &exc_value, &exc_traceback);

            if (exc_traceback == NULL) {
                crash_dialog("Could not retrieve traceback");
                fclose(log);
                exit(-5);
            }

            if (PyErr_GivenExceptionMatches(exc_value, PyExc_SystemExit)) {
                systemExit_code = PyObject_GetAttrString(exc_value, "code");
                if (systemExit_code == NULL) {
                    std::wcout << "Could not determine exit code" << std::endl;
                    ret = -10;
                }
                else {
                    ret = (int) PyLong_AsLong(systemExit_code);
                }
            } else {
                ret = -6;
            }

            if (ret != 0) {
                // Display stack trace in the crash dialog.
                traceback_str = format_traceback(exc_type, exc_value, exc_traceback);
                crash_dialog(traceback_str);

                std::wcout << "===========================================================================" << std::endl;
                std::wcout << "Application quit abnormally (Exit code " << ret << ")!" << std::endl;

                // Restore the error state of the interpreter.
                PyErr_Restore(exc_type, exc_value, exc_traceback);

                // Print exception to stderr.
                // In case of SystemExit, this will call exit()
                PyErr_Print();

                exit(ret);
            }
        }
    }
    catch (Exception^ exception) {
        crash_dialog("Python runtime error: " + exception);
        ret = -7;
    }
    fclose(log);
    Py_Finalize();

    return ret;
}

wchar_t *wstr(String^ str)
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
    std::wcout << wstr(details);

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
        std::wcout << "Could not import traceback" << std::endl;
        return "Could not import traceback";
    }

    format_exception = PyObject_GetAttrString(traceback_module, "format_exception");
    if (format_exception && PyCallable_Check(format_exception)) {
        traceback_list = PyObject_CallFunctionObjArgs(format_exception, type, value, traceback, NULL);
    } else {
        std::wcout << "Could not find 'format_exception' in 'traceback' module" << std::endl;
        return "Could not find 'format_exception' in 'traceback' module";
    }
    if (traceback_list == NULL) {
        std::wcout << "Could not format traceback" << std::endl;
        return "Could not format traceback";
    }

    // Concatenate all the lines of the traceback into a single string
    traceback_unicode = PyUnicode_Join(PyUnicode_FromString(""), traceback_list);

    // Convert the Python Unicode string into a UTF-16 Windows String.
    // It's easiest to do this by using the Python API to encode to UTF-8,
    // and then convert the UTF-8 byte string into UTF-16 using Windows APIs.
    Py_ssize_t size;
    const char* bytes = PyUnicode_AsUTF8AndSize(PyObject_Str(traceback_unicode), &size);

    System::Text::UTF8Encoding^ utf8 = gcnew System::Text::UTF8Encoding;
    traceback_str = gcnew String(utf8->GetString((Byte *)bytes, (int) size));

    // Clean up the traceback string, removing references to the installed app location
    traceback_str = traceback_str->Replace(Application::StartupPath, "");

    return traceback_str;
}
