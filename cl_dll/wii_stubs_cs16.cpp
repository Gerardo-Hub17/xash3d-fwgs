#include <cstddef>
// wii / Xash3D FWGS: stubs para enlace estatico.
// El motor en Wii no usa el patron CreateInterface/InterfaceReg; proveemos
// los simbolos minimos que el cliente CS16 referencia en cdll_int.cpp.

#include "interface.h"

InterfaceReg *InterfaceReg::s_pInterfaceRegs = NULL;

InterfaceReg::InterfaceReg( InstantiateInterfaceFn fn, const char *pName )
    : m_CreateFn( fn ), m_pName( pName )
{
    m_pNext = s_pInterfaceRegs;
    s_pInterfaceRegs = this;
}

CreateInterfaceFn Sys_GetFactoryThis( void )
{
    return NULL;
}
