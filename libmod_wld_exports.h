/*  
 * WLD Module Exports for BennuGD2  
 */  
  
#ifndef __LIBMOD_WLD_EXPORTS  
#define __LIBMOD_WLD_EXPORTS  
  
#include "bgddl.h"  
  
#if defined(__BGDC__) || !defined(__STATIC__)  
  
#include "libmod_wld.h"  
  
/* Constantes exportadas */  
DLCONSTANT __bgdexport(libmod_wld, constants_def)[] = {  
    {NULL, 0, 0}};  
  
DLSYSFUNCS __bgdexport(libmod_wld, functions_exports)[] = {  

// funciones de camara para los sprites  
 FUNC("WLD_SET_CAMERA", "IIIIII", TYPE_INT, libmod_wld_set_camera),  

// Mapas DMAP (tile-based)  
FUNC("GET_TEX_IMAGE", "I", TYPE_INT, get_tex_image),
FUNC("LOAD_WLD", "SI", TYPE_INT, libmod_wld_load_wld),
FUNC("RENDER_WLD_2D", "II", TYPE_INT, libmod_wld_render_wld_2d),
FUNC("HEIGHTMAP_RENDER_WLD_3D", "IIF", TYPE_INT, libmod_wld_render_wld_3d),
FUNC("HEIGHTMAP_SET_WLD_FOV", "F", TYPE_INT, libmod_wld_set_wld_fov),
FUNC("TEST_RENDER_BUFFER", "II", TYPE_INT, libmod_wld_test_render_buffer),
FUNC(0, 0, 0, 0)};  
  
#endif  
  
/* Hooks del módulo */  
void __bgdexport(libmod_wld, module_initialize)();  
void __bgdexport(libmod_wld, module_finalize)();  
  
#endif