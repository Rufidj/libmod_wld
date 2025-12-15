#ifndef __LIBMOD_WLD_H                
#define __LIBMOD_WLD_H   

#include <stdint.h>                
#include <math.h>                
    
/* Inclusiones necesarias de BennuGD2 */                
#include "bgddl.h"                
#include "libbggfx.h"                
#include "g_bitmap.h"                
#include "g_blit.h"                
#include "g_pixel.h"                
#include "g_clear.h"                
#include "g_grlib.h"                
#include "xstrings.h"                
#include "m_map.h"                
#include <GL/glew.h>     


    
#define MAX_POINTS  8192  
#define MAX_WALLS   8192  
#define MAX_REGIONS 4096  
#define MAX_FLAGS   1000

#define DEFAULT_FOV 60.0f
#define DEFAULT_NEAR 1.0f
#define DEFAULT_FAR 10000.0f
    
  
// ============================================================================      
// FORMATO TEX - Sistema de texturas simple      
// ============================================================================    
    
// Header para formato TEX    
typedef struct {    
    char magic[4];    
    uint32_t version;    
    uint16_t num_images;    
    uint8_t reserved[6];    
} TEX_HEADER;    
    
typedef struct {    
    uint16_t index;    
    uint16_t width;    
    uint16_t height;    
    uint16_t format;    
    uint8_t reserved[250];    
} TEX_ENTRY;    
    
      
// Estructura de textura para SECTOR      
typedef struct {      
    char filename[256];      
    int graph_id;      
} SECTOR_TEXTURE_ENTRY;      

// Estructuras mínimas para WLD (añadir a libmod_heightmap.h)  
typedef struct {  
    int code;  
    int Width, Height;  
    int Width2, Height2;  
    char *Raw;  
    int Used;  
} WLD_PicInfo;  
  
typedef struct {  
    WLD_PicInfo *pPic;  
    int code;  
} WLD_TexCon;  

#pragma pack(push, 1)  
  
typedef struct {  
    int active;  
    int depth;  
} RaySegment;

typedef struct {  
    int sector_idx;  
    float min_distance;  
    float max_distance;  
    int visible;  
    int x, y;  
    int number;  
} WLD_Flag;  // Total: 12 bytes  
  
typedef struct {  
    int32_t active;  
    int32_t x, y;  
    int32_t links;  
} WLD_Point;  // Compatible con tpoint  
  
typedef struct {  
    int32_t active;  
    int32_t type;  
    int32_t p1, p2;  
    int32_t front_region, back_region;  
    int32_t texture;  
    int32_t texture_top;  
    int32_t texture_bot;  
    int32_t fade;  
} WLD_Wall;  // Compatible con twall
  
typedef struct {  
    int active;  
    int type;  
    int floor_height, ceil_height;  
    int floor_tex;  
    int ceil_tex;  
    int fade;  
} WLD_Region;  // Total: 28 bytes - NO MODIFICAR (se lee con fread)
  

typedef struct {  
    WLD_Region *original_region;
    WLD_Wall **wall_ptrs;
    int num_wall_ptrs;
    int capacity;
    
    // Pre-computed nested sectors
    int nested_regions[64];
    int num_nested_regions;
    
    // Sistema de jerarquía de regiones (estilo DIV VPE)
    // MOVIDO AQUÍ para no alterar el tamaño de WLD_Region
    int above_region;  // Índice de región superior (-1 si no hay)
    int below_region;  // Índice de región inferior (-1 si no hay)
} WLD_Region_Optimized;
  
#pragma pack(pop)

// VDraw structure - similar to DIV VPE
// Represents a drawable element (wall or object) found during scan phase
typedef struct {
    int type;           // 0=simple wall, 1=complex wall (portal), 2=object
    WLD_Wall *wall;     // Wall pointer (if type 0 or 1)
    int region_idx;     // Region this element belongs to
    int left_col;       // Left screen column
    int right_col;      // Right screen column
    float distance;     // Distance from camera
    float px1, px2;     // Projected distances at left and right
    
    // Clipping info
    int clip_top;
    int clip_bottom;
    
    // For complex walls (portals)
    int adjacent_region;
} WLD_VDraw;

#define MAX_VDRAWS 4096  // Maximum drawable elements per frame
  
typedef struct {  
    int num_points;  
    WLD_Point **points;     // Array de punteros  
    int num_walls;  
    WLD_Wall **walls;       // Array de punteros  
    int num_regions;  
    WLD_Region **regions;   // Array de punteros  
    int num_flags;  
    WLD_Flag **flags;       // Array de punteros  
    int loaded;  
    int skybox_angle;  
    char skybox_texture[32]; 
} WLD_Map;

typedef struct {  
    int floor_height, ceil_height;  
    int floor_tex, ceil_tex;  
    int num_walls;  
    WLD_Wall **walls;  
    int *neighbors;  // Regiones adyacentes (portales)  
} WLD_Sector;  


typedef struct {  
    int sector_idx;  
    float min_distance;  
    float max_distance;  
    int visible;  
} SectorVisibility;  
  typedef struct {  
    uint32_t final_color;  // Color final con transparencia aplicada  
    uint8_t has_water;     // 1 si hay agua, 0 si no  
    float water_depth;     // Profundidad del agua para efectos  
} PRECALC_WATER_DATA;  
  
typedef struct {                
    float x, y, z;                
    float angle, pitch;                
    float fov;                
    float near, far;        
    float plane_x, plane_y;        
} CAMERA_3D;            
      
typedef struct {                  
    int screen_x, screen_y;                  
    float distance;            
    float distance_scale;            
    int scaled_width, scaled_height;                  
    uint8_t alpha;                  
    int valid;                  
    float fog_tint_factor;              
} BILLBOARD_PROJECTION;     

static int wld_fpg_id = 0;  
static void **wld_fpg_grf = NULL;
static WLD_Map wld_map = {0};  
static GRAPH *render_buffer = NULL;
CAMERA_3D camera = {0, 0, 0, 0, 0, DEFAULT_FOV, DEFAULT_NEAR, DEFAULT_FAR};

static WLD_Sector *sectors = NULL;
  
// Variables globales para el sistema WLD  
static WLD_PicInfo *wld_pics[1000];  
static int wld_num_pics = 0;

// Mover estas líneas al principio del archivo (después de los #include)  
#define MAXSECTORS 1024  
#define MAXWALLS 8192  
#define MAXSPRITES 4096  
#define MAXTILES 9216

#define RGBA32_R(color) ((color >> 16) & 0xFF)
#define RGBA32_G(color) ((color >> 8) & 0xFF)
#define RGBA32_B(color) (color & 0xFF)
#define RGBA32_A(color) ((color >> 24) & 0xFF)

// Variables globales para el color del cielo
static Uint8 sky_color_r = 135; // Azul cielo por defecto
static Uint8 sky_color_g = 206;
static Uint8 sky_color_b = 235;
static Uint8 sky_color_a = 255; // Alpha del cielo

// Distancia de renderizado
static float max_render_distance = 12000.0f;

// Variables de control de sensibilidad  
static float wld_mouse_sensitivity = 50.0f;  
static float wld_move_speed = 10.0f;  
static float wld_height_speed = 3.0f;



// Declaración por si no está en el header
void gr_alpha_put_pixel(GRAPH *dest, int x, int y, uint32_t color, uint8_t alpha);
GRAPH *get_tex_image(int index);
extern int64_t libmod_heightmap_load_wld(INSTANCE *my, int64_t *params);
extern int64_t libmod_heightmap_render_wld_2d(INSTANCE *my, int64_t *params);
extern int64_t libmod_heightmap_test_render_buffer(INSTANCE *my, int64_t *params);
extern void wld_analyze_x_distribution(WLD_Map *map);  
extern void wld_debug_walls_with_x_diff(WLD_Map *map);
// Funciones 3D WLD (renderizado VPE sin dependencias externas)  
extern void render_wld(WLD_Map *map, int screen_w, int screen_h);
extern int point_in_region(float x, float y, int region_idx, WLD_Map *map);
extern void scan_walls_from_region(WLD_Map *map, int region_idx, float cam_x, float cam_y,    
                                  float ray_dir_x, float ray_dir_y, float *hit_distance,    
                                  WLD_Wall **hit_wall, int *hit_region, int *adjacent_region);  
extern void render_wall_column(WLD_Map *map, WLD_Wall *wall, int region_idx,   
                              int col, int screen_w, int screen_h,  
                              float cam_x, float cam_y, float cam_z, float distance,
                              int clip_top, int clip_bottom);  
extern float intersect_ray_segment(float ray_dir_x, float ray_dir_y, float cam_x, float cam_y,  
                                   float seg_x1, float seg_y1, float seg_x2, float seg_y2);
extern int wld_find_region(WLD_Map *map, float x, float y, int discard_region);
// Función exportada para BennuGD2  
extern int64_t libmod_heightmap_render_wld_3d(INSTANCE *my, int64_t *params);
extern void render_floor_and_ceiling(WLD_Map *map, WLD_Region *region, int region_idx, int col,  
                                     int screen_w, int screen_h, int wall_top, int wall_bottom,  
                                     float cam_x, float cam_y, float cam_z, float distance,  
                                     float fog_factor, int clip_top, int clip_bottom, float angle_offset); 
extern void render_complex_wall_section(WLD_Map *map, WLD_Wall *wall, WLD_Region *region,  
                                        int region_idx, int col, int screen_w, int screen_h,  
                                        int y_start, int y_end, float wall_u, float fog_factor,  
                                        float camera_x, float camera_y, float camera_z,   
                                        float hit_distance, int clip_top, int clip_bottom);
extern void render_wall_section(WLD_Map *map, int texture_index, int col,   
                        int y_start, int y_end, float wall_u, float fog_factor,   
                        char *section_name, float orig_top, float orig_height);
extern void wld_build_wall_ptrs(WLD_Map *map);
static bool vertices_equal(int32_t x1, int32_t y1, int32_t x2, int32_t y2) {  
    return x1 == x2 && y1 == y2;  
}
extern int64_t libmod_wld_look_horizontal(INSTANCE *my, int64_t *params);  
extern int64_t libmod_wld_look_vertical(INSTANCE *my, int64_t *params);  
extern int64_t libmod_wld_ajust_height(INSTANCE *my, int64_t *params);


// Declaraciones de camara

extern int64_t libmod_heightmap_set_camera(INSTANCE *my, int64_t *params);   

#endif