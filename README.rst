Briefcase Windows Visual Studio Template
========================================

A `Cookiecutter <https://github.com/cookiecutter/cookiecutter/>`__ template for
building Python apps from a Visual Studio project, that can then be packaged
with an MSI installer.

**This repository branch contains a template for Python 3.11**.
Other Python versions are available by cloning other branches of repository.

Using this template
-------------------

The easiest way to use this project is to not use it at all - at least, not
directly. `Briefcase <https://github.com/beeware/briefcase/>`__ is a tool that
uses this template, rolling it out using data extracted from a
``pyproject.toml`` configuration file.

However, if you *do* want use this template directly...

1. Install `cookiecutter`_. This is a tool used to bootstrap complex project
   templates::

    $ pip install cookiecutter

2. Run ``cookiecutter`` on the template::

    $ cookiecutter https://github.com/beeware/briefcase-windows-visualstudio-template --checkout 3.11

   This will ask you for a number of details of your application, including the
   `name` of your application (which should be a valid PyPI identifier), and
   the `Formal Name` of your application (the full name you use to describe
   your app). The remainder of these instructions will assume a `name` of
   ``my-project``, and a formal name of ``My Project``.

3. Build the project. You can do this by opening the solution file in Visual
   Studio and building the Release configuration, or by using MSBuild:

    $ MSBuild.exe "My Project.sln" -target:restore -property:RestorePackagesConfig=true -target:"My Project" -property:Configuration=Release

   In order to build the project, you will need to configure Visual Studio
   to include the following modules:

   * **.NET Desktop development** - default options
   * **Deskop Development with C++** - default options, plus the "C++/CLI Support for v143 build tools".

4. `Download the Python Embedded Windows install`_, and extract it into the
   ``My Project/x64/Release`` directory generated by the template. This will give you a
   ``My Project/x64/Release/python`` directory containing a self-contained Python
   install.

5. Add your code to the template, into the ``My Project/x64/Release/app`` directory.
   At the very minimum, you need to have an ``app/<app name>/__main__.py`` file
   that defines an entry point that will start your application.

   If your code has any dependencies, they should be installed under the
   ``My Project/x64/Release/app_packages`` directory.

If you've done this correctly, a project with a formal name of ``My Project``,
with an app name of ``my-project`` should have a directory structure that
looks something like::

    My Project/
        x64/
            Release/
                app/
                    my_project/
                        __init__.py
                        __main__.py
                        app.py
                app_packages/
                    ...
                python/
                    python3.dll
                    ...
                My Project.exe
                My Project.exe.metagen
                My Project.pdb
        My Project/
            My Project.vcxproj
            My Project.vcxproj.filters
            ...
        My Project.sln
        briefcase.toml
        my-project.wxs
        unicode.wxl

The executable in ``x64/Release`` will start your application.

This project can now be compiled with `WiX <https://wixtoolset.org>`__ to
produce an MSI file. This is a three step process. Open a command prompt,
and change into the ``My Project`` directory. Then:

1. Generate a manifest of the files in your project::

    C:\...>"%WIX%\bin\heat.exe" dir x64/Release -gg -sfrag -sreg -srd -scom -dr my_project_ROOTDIR -cg my_project_COMPONENTS -var var.SourceDir -out my-project-manifest.wxs

2. Compile the ``.wxs`` files::

    C:\...>"%WIX%\bin\candle.exe" -ext WixUtilExtension -ext WixUIExtension -dSourceDir=x64/Release -arch x64 my-project.wxs my-project-manifest.wxs

3. Link the compiled output to produce the MSI::

    C:\...>"%WIX%\bin\light.exe" -ext WixUtilExtension -ext WixUIExtension -loc unicode.wxl my-project.wixobj my-project-manifest.wixobj -o "My Project.msi"

The MSI file can then be used to install your application. When installed, your
application will have an entry in your Start menu.

Next steps
----------

Of course, running Python code isn't very interesting by itself - you won't
be able to do any console input or output, because a Windows app doesn't
display a console.

To do something interesting, you'll need to work with the native Windows system
libraries to draw widgets and respond to screen taps. The `Python for .NET`_
bridging library can be used to interface with the Windows system libraries.
Alternatively, you could use a cross-platform widget toolkit that supports
Windows (such as `Toga`_) to provide a GUI for your application.

If you have any external library dependencies (like Toga, or anything other
third-party library), you should install the library code into the
``app_packages`` directory. This directory is the same as a  ``site_packages``
directory on a desktop Python install.

.. _cookiecutter: https://github.com/cookiecutter/cookiecutter
.. _Download the Python Embedded Windows install: https://briefcase-support.org/python?platform=windows&version=3.11
.. _Python for .NET: http://pythonnet.github.io/
.. _Toga: https://beeware.org/project/projects/libraries/toga
