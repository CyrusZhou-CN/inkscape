

#include "InkscapePython.h"

#include <Python.h>

#include <stdio.h>

#include "inkscape_py.py.h"

/*
 * Generated by SWIG
 */
extern "C" void init_inkscape_py(void);



/*
 *
 */
InkscapePython::InkscapePython()
{
}

    

/*
 *
 */
InkscapePython::~InkscapePython()
{

}

    
    

/*
 *  Interpret an in-memory string
 */
bool InkscapePython::interpretString(char *codeStr)
{
    Py_Initialize();
    init_inkscape_py();
    PyRun_SimpleString(inkscape_module_script);
    PyRun_SimpleString("inkscape = _inkscape_py.getInkscape()\n");
    PyRun_SimpleString(codeStr);
    Py_Finalize();
}

    
    

/*
 *  Interpret a named file
 */
bool InkscapePython::interpretFile(char *fileName)
{
    Py_Initialize();
    init_inkscape_py();
    PyRun_SimpleString(inkscape_module_script);
    PyRun_SimpleString("inkscape = _inkscape_py.getInkscape()\n");
    FILE *f = fopen(fileName, "r");
    PyRun_SimpleFile(f, fileName);
    fclose(f);
    Py_Finalize();

}

