// DummyDll.cpp : Defines the exported functions for the DLL.
//

#include "pch.h"
#include "framework.h"
#include "DummyDll.h"


// This is an example of an exported variable
DUMMYDLL_API int nDummyDll=0;

// This is an example of an exported function.
DUMMYDLL_API int fnDummyDll(void)
{
    return 0;
}

// This is the constructor of a class that has been exported.
CDummyDll::CDummyDll()
{
    return;
}
