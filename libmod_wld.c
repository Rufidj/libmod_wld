#include "libmod_wld.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <stdio.h>
#include "libbggfx.h"  
#include <GL/glew.h>
#include <inttypes.h>  
#include "tex_format.h"
#include <limits.h> 

#define max(a,b) ((a) > (b) ? (a) : (b))  
#define min(a,b) ((a) < (b) ? (a) : (b))  
#define VERTEX_TOLERANCE 0.001f 
// Agregar después de las variables globales WLD (línea ~125)  
static WLD_Region_Optimized optimized_regions[MAX_REGIONS];  
static WLD_Wall *wall_ptrs[MAX_WALLS];

// fov configurable WLD
static float wld_angle_step = 0.005f;
static float wld_focal_length = 180.0f; // 0.9 / 0.005

// Caché espacial para point_in_region()  
#define CACHE_SIZE 1024  
#define CACHE_SCALE 0.1f  // 1 unidad = 10 unidades del mundo  
  
typedef struct {  
    int region_idx;  
    int valid;  
} RegionCacheEntry;  
  
static RegionCacheEntry region_cache[CACHE_SIZE][CACHE_SIZE]; 
// Si aún tienes problemas, puedes definir manualmente los offsets:  
#ifndef CTYPE  
enum {  
    CTYPE = 0,  
    CNUMBER,  
    COORDX,  
    COORDY,  
    COORDZ,  
    FILEID,  
    GRAPHID,  
    GRAPHSIZE,  
    ANGLE,  
    FLAGS,  
    REGIONID,  
    RESOLUTION,  
    GRAPHSIZEX,  
    GRAPHSIZEY,  
    XGRAPH,  
    _OBJECTID,  
    XGRAPH_FLAGS,  
    STATUS,  
    PROCESS_ID,  
    RENDER_FILEID,  
    RENDER_GRAPHID  
};  
#endif

#define MAX_BILLBOARDS 1000  
#define MAX_STATIC_BILLBOARDS 500  
#define MAX_DYNAMIC_BILLBOARDS 500  

// Atributos para VDraws  
#define VD_OBJECT 0x10000000  
#define VD_COMPLEX 0x20000000  
#define VD_WALLBACK 0x40000000

// Variable global para almacenar gráficos FPG cargados  
static GRAPH **wld_fpg_graphics = NULL;  
static int wld_fpg_count = 0;  
  
// Para clipping por columna  
typedef struct {  
    short Top, Bot;  
} VLine;  
  
// Para líneas de piso  
typedef struct {  
    unsigned char *RawPtr, *PalPtr, *PixPtr;  
    unsigned int Coord, Delta;  
    short LeftCol, Count, Width2;  
    void *pRegion;  
} FLine;  
  
// Para líneas con texturas (paredes/máscaras)  
typedef struct {  
    unsigned char *RawPtr, *PalPtr, *PixPtr;  
    int Coord, Delta;  
    short BufWidth, Count, Mask;  
    unsigned char IsTrans, IsTile;  
} WLine;  
  
// Elemento a renderizar  
typedef struct {  
    unsigned int Type;  
    void *ptr;  
    struct VDraw *Prev, *Next;  
    short LeftCol, RightCol;  
    int LD, RD, dLD, dRD;  
    int XStart, XLen;  
    int px1, px2;  
} VDraw;  
  
// Nivel de renderizado  
typedef struct {  
    void *Start;  
    void *From;  
    VLine *Clip;  
    int ClipCount;  
    int MinDist;  
} Level;

// Arrays para el sistema de renderizado  
VDraw *VDraws;  
WLine *MLines;  
FLine *FLines;  
VLine *VLines;  
Level Levels[64];  
Level *CurLevel;  
VDraw LeftVDraw, RightVDraw;  
  
int NumVDraws, NumMLines, NumLevels;

typedef struct {    
    int active;    
    float world_x, world_y, world_z;    
    int graph_id;    
    int64_t process_id;  
    float scale;  
    int billboard_type;  // NUEVO CAMPO  
} VOXEL_BILLBOARD;



// Estructura temporal para ordenamiento de billboards  
typedef struct {  
    VOXEL_BILLBOARD *billboard;  
    BILLBOARD_PROJECTION projection;  
    GRAPH *graph;  
    float distance;  
} BILLBOARD_RENDER_DATA;
  

  
#ifndef M_PI_2
#define M_PI_2 1.57079632679f
#endif

// Forward declarations
void export_map_to_text(WLD_Map *map, const char *filename);
void analyze_wall_coordinates(WLD_Map *map);
int wld_process_geometry(FILE *fichero, WLD_Map *map);
void wld_assign_regions_simple(WLD_Map *map, int target_region);
void wld_build_sectors(WLD_Map *map);
void validate_and_fix_portals(WLD_Map *map);
void debug_current_portals(WLD_Map *map);

/* Configurar cámara 3D - Original */
int64_t libmod_wld_set_camera(INSTANCE *my, int64_t *params)  
{  
    fprintf(stderr, "DEBUG SET_CAMERA: params[0]=%lld params[1]=%lld params[2]=%lld\n",   
            params[0], params[1], params[2]);  
      
    camera.x = (float)params[0];  
    camera.y = (float)params[1];  
    camera.z = (float)params[2];  
    camera.angle = (float)params[3] / 1000.0f;  
    camera.pitch = (float)params[4] / 1000.0f;  
    camera.fov = (params[5] > 0) ? (float)params[5] : DEFAULT_FOV;  
      
    fprintf(stderr, "DEBUG SET_CAMERA: camera.x=%.2f camera.y=%.2f camera.z=%.2f\n",  
            camera.x, camera.y, camera.z);  
  
    const float max_pitch = M_PI_2 * 0.99f;  
    if (camera.pitch > max_pitch)  
        camera.pitch = max_pitch;  
    if (camera.pitch < -max_pitch)  
        camera.pitch = -max_pitch;  
  
    return 1;  
}


// Movimiento de camara

int64_t libmod_wld_look_horizontal(INSTANCE *my, int64_t *params)  
{  
    float delta = (float)params[0];  
    camera.angle += delta * wld_mouse_sensitivity / 1000.0f;  
    return 1;  
}

int64_t libmod_wld_look_vertical(INSTANCE *my, int64_t *params)  
{  
    float delta = (float)params[0];  
    camera.pitch -= delta * wld_mouse_sensitivity / 1000.0f;  
      
    // Limitar pitch para evitar volteo de cámara  
    const float max_pitch = M_PI_2 * 0.99f;  
    if (camera.pitch > max_pitch)  
        camera.pitch = max_pitch;  
    if (camera.pitch < -max_pitch)  
        camera.pitch = -max_pitch;  
      
    return 1;  
}

int64_t libmod_wld_ajust_height(INSTANCE *my, int64_t *params)  
{  
    float delta = (float)params[0];  
    float new_z = camera.z + delta * wld_height_speed / 2.0f;  
      
    // Encontrar región actual para verificar límites  
    int current_region = -1;  
    for (int i = 0; i < wld_map.num_regions; i++) {  
        if (point_in_region(camera.x, camera.y, i, &wld_map)) {  
            current_region = i;  
            break;  
        }  
    }  
      
    if (current_region != -1) {  
        WLD_Region *region = wld_map.regions[current_region];  
        if (region && region->active) {  
            // Limitar entre piso y techo con margen  
            if (new_z > region->floor_height + 10 && new_z < region->ceil_height - 10) {  
                camera.z = new_z;  
            }  
        }  
    } else {  
        // Si no hay región, aplicar límite mínimo  
        camera.z = new_z;  
        if (camera.z < 20) camera.z = 20;  
    }  
      
    return 1;  
}



int64_t libmod_wld_move_forward(INSTANCE *my, int64_t *params)  
{  
    float speed = (params[0] > 0) ? (float)params[0] : wld_move_speed;  
    float dx = cosf(camera.angle) * speed / 1.5f;  
    float dy = sinf(camera.angle) * speed / 1.5f;  
      
    float new_x = camera.x + dx;  
    float new_y = camera.y + dy;  
      
    // Encontrar región actual  
    int current_region = -1;  
    for (int i = 0; i < wld_map.num_regions; i++) {  
        if (point_in_region(camera.x, camera.y, i, &wld_map)) {  
            current_region = i;  
            break;  
        }  
    }  
      
    if (current_region == -1) return 0;  
      
    // Verificar colisión con paredes  
    WLD_Region_Optimized *opt_region = &optimized_regions[current_region];  
    bool collision = false;  
      
    for (int i = 0; i < opt_region->num_wall_ptrs; i++) {  
        WLD_Wall *wall = opt_region->wall_ptrs[i];  
        if (!wall) continue;  
          
        float x1 = wld_map.points[wall->p1]->x;  
        float y1 = wld_map.points[wall->p1]->y;  
        float x2 = wld_map.points[wall->p2]->x;  
        float y2 = wld_map.points[wall->p2]->y;  
          
        float t = intersect_ray_segment(dx, dy, camera.x, camera.y, x1, y1, x2, y2);  
          
        if (t > 0 && t < 1.0f) {  
            if (wall->back_region == -1) {  
                collision = true;  
                break;  
            } else {  
                WLD_Region *current = wld_map.regions[current_region];  
                WLD_Region *adjacent = wld_map.regions[wall->back_region];  
                  
                int max_floor = (current->floor_height > adjacent->floor_height)   
                    ? current->floor_height : adjacent->floor_height;  
                int min_ceil = (current->ceil_height < adjacent->ceil_height)   
                    ? current->ceil_height : adjacent->ceil_height;  
                  
                if (camera.z < max_floor || camera.z > min_ceil) {  
                    collision = true;  
                    break;  
                }  
            }  
        }  
    }  
      
    if (!collision) {  
        camera.x = new_x;  
        camera.y = new_y;  
    }  
      
    return 1;  
}

int64_t libmod_wld_move_backward(INSTANCE *my, int64_t *params)  
{  
    float speed = (params[0] > 0) ? (float)params[0] : wld_move_speed;  
    float dx = cosf(camera.angle + M_PI) * speed / 1.5f;  
    float dy = sinf(camera.angle + M_PI) * speed / 1.5f;  
      
    float new_x = camera.x + dx;  
    float new_y = camera.y + dy;  
      
    // Encontrar región actual  
    int current_region = -1;  
    for (int i = 0; i < wld_map.num_regions; i++) {  
        if (point_in_region(camera.x, camera.y, i, &wld_map)) {  
            current_region = i;  
            break;  
        }  
    }  
      
    if (current_region == -1) return 0;  
      
    // Verificar colisión con paredes  
    WLD_Region_Optimized *opt_region = &optimized_regions[current_region];  
    bool collision = false;  
      
    for (int i = 0; i < opt_region->num_wall_ptrs; i++) {  
        WLD_Wall *wall = opt_region->wall_ptrs[i];  
        if (!wall) continue;  
          
        float x1 = wld_map.points[wall->p1]->x;  
        float y1 = wld_map.points[wall->p1]->y;  
        float x2 = wld_map.points[wall->p2]->x;  
        float y2 = wld_map.points[wall->p2]->y;  
          
        float t = intersect_ray_segment(dx, dy, camera.x, camera.y, x1, y1, x2, y2);  
          
        if (t > 0 && t < 1.0f) {  
            if (wall->back_region == -1) {  
                collision = true;  
                break;  
            } else {  
                WLD_Region *current = wld_map.regions[current_region];  
                WLD_Region *adjacent = wld_map.regions[wall->back_region];  
                  
                int max_floor = (current->floor_height > adjacent->floor_height)   
                    ? current->floor_height : adjacent->floor_height;  
                int min_ceil = (current->ceil_height < adjacent->ceil_height)   
                    ? current->ceil_height : adjacent->ceil_height;  
                  
                if (camera.z < max_floor || camera.z > min_ceil) {  
                    collision = true;  
                    break;  
                }  
            }  
        }  
    }  
      
    if (!collision) {  
        camera.x = new_x;  
        camera.y = new_y;  
    }  
      
    return 1;  
}

int64_t libmod_wld_strafe_left(INSTANCE *my, int64_t *params)  
{  
    float speed = (params[0] > 0) ? (float)params[0] : wld_move_speed;  
    float dx = cosf(camera.angle - M_PI_2) * speed / 2.0f;  
    float dy = sinf(camera.angle - M_PI_2) * speed / 2.0f;  
      
    float new_x = camera.x + dx;  
    float new_y = camera.y + dy;  
      
    // Encontrar región actual  
    int current_region = -1;  
    for (int i = 0; i < wld_map.num_regions; i++) {  
        if (point_in_region(camera.x, camera.y, i, &wld_map)) {  
            current_region = i;  
            break;  
        }  
    }  
      
    if (current_region == -1) return 0;  
      
    // Verificar colisión con paredes  
    WLD_Region_Optimized *opt_region = &optimized_regions[current_region];  
    bool collision = false;  
      
    for (int i = 0; i < opt_region->num_wall_ptrs; i++) {  
        WLD_Wall *wall = opt_region->wall_ptrs[i];  
        if (!wall) continue;  
          
        float x1 = wld_map.points[wall->p1]->x;  
        float y1 = wld_map.points[wall->p1]->y;  
        float x2 = wld_map.points[wall->p2]->x;  
        float y2 = wld_map.points[wall->p2]->y;  
          
        float t = intersect_ray_segment(dx, dy, camera.x, camera.y, x1, y1, x2, y2);  
          
        if (t > 0 && t < 1.0f) {  
            if (wall->back_region == -1) {  
                collision = true;  
                break;  
            } else {  
                WLD_Region *current = wld_map.regions[current_region];  
                WLD_Region *adjacent = wld_map.regions[wall->back_region];  
                  
                int max_floor = (current->floor_height > adjacent->floor_height)   
                    ? current->floor_height : adjacent->floor_height;  
                int min_ceil = (current->ceil_height < adjacent->ceil_height)   
                    ? current->ceil_height : adjacent->ceil_height;  
                  
                if (camera.z < max_floor || camera.z > min_ceil) {  
                    collision = true;  
                    break;  
                }  
            }  
        }  
    }  
      
    if (!collision) {  
        camera.x = new_x;  
        camera.y = new_y;  
    }  
      
    return 1;  
}

int64_t libmod_wld_strafe_right(INSTANCE *my, int64_t *params)  
{  
    float speed = (params[0] > 0) ? (float)params[0] : wld_move_speed;  
    float dx = cosf(camera.angle + M_PI_2) * speed / 2.0f;  
    float dy = sinf(camera.angle + M_PI_2) * speed / 2.0f;  
      
    float new_x = camera.x + dx;  
    float new_y = camera.y + dy;  
      
    // Encontrar región actual  
    int current_region = -1;  
    for (int i = 0; i < wld_map.num_regions; i++) {  
        if (point_in_region(camera.x, camera.y, i, &wld_map)) {  
            current_region = i;  
            break;  
        }  
    }  
      
    if (current_region == -1) return 0;  
      
    // Verificar colisión con paredes  
    WLD_Region_Optimized *opt_region = &optimized_regions[current_region];  
    bool collision = false;  
      
    for (int i = 0; i < opt_region->num_wall_ptrs; i++) {  
        WLD_Wall *wall = opt_region->wall_ptrs[i];  
        if (!wall) continue;  
          
        float x1 = wld_map.points[wall->p1]->x;  
        float y1 = wld_map.points[wall->p1]->y;  
        float x2 = wld_map.points[wall->p2]->x;  
        float y2 = wld_map.points[wall->p2]->y;  
          
        float t = intersect_ray_segment(dx, dy, camera.x, camera.y, x1, y1, x2, y2);  
          
        if (t > 0 && t < 1.0f) {  
            if (wall->back_region == -1) {  
                collision = true;  
                break;  
            } else {  
                WLD_Region *current = wld_map.regions[current_region];  
                WLD_Region *adjacent = wld_map.regions[wall->back_region];  
                  
                int max_floor = (current->floor_height > adjacent->floor_height)   
                    ? current->floor_height : adjacent->floor_height;  
                int min_ceil = (current->ceil_height < adjacent->ceil_height)   
                    ? current->ceil_height : adjacent->ceil_height;  
                  
                if (camera.z < max_floor || camera.z > min_ceil) {  
                    collision = true;  
                    break;  
                }  
            }  
        }  
    }  
      
    if (!collision) {  
        camera.x = new_x;  
        camera.y = new_y;  
    }  
      
    return 1;  
}



GRAPH *get_tex_image(int index)     
{    
    // CAMBIAR: Permitir ID 0 como válido en BennuGD2  
    if (index < 1) {    
        return NULL;    
    }    
        
    // Obtener textura del FPG usando bitmap_get    
    GRAPH *graph = bitmap_get(wld_fpg_id, index);    
    return graph;    
}
  

void wld_tex_alloc(WLD_TexCon *ptc, int texcode)  
{  
    WLD_PicInfo *pic;  
    int i;  
  
    ptc->pPic = NULL;  
    if (!texcode) return;  
  
    // Buscar si ya está cargada  
    for(i = 0; i < wld_num_pics; i++) {  
        pic = wld_pics[i];  
        if (pic && texcode == pic->code) {  
            ptc->pPic = pic;  
            return;  
        }  
    }  
  
    // Cargar desde sistema TEX  
    GRAPH *tex_graph = get_tex_image(texcode);  
    if (!tex_graph) return;  
  
    // Crear nueva entrada  
    pic = malloc(sizeof(WLD_PicInfo));  
    if (!pic) return;  
  
    pic->code = texcode;  
    pic->Width = tex_graph->width;  
    pic->Height = tex_graph->height;  
    pic->Width2 = 0;  
    pic->Height2 = 0;  
    pic->Raw = NULL;  
    pic->Used = 0;  
  
    // Calcular Width2 (log2 del ancho)  
    switch(pic->Width) {  
        case   2: pic->Width2 = 1; break;  
        case   4: pic->Width2 = 2; break;  
        case   8: pic->Width2 = 3; break;  
        case  16: pic->Width2 = 4; break;  
        case  32: pic->Width2 = 5; break;  
        case  64: pic->Width2 = 6; break;  
        case 128: pic->Width2 = 7; break;  
        case 256: pic->Width2 = 8; break;  
        case 512: pic->Width2 = 9; break;  
        case 1024: pic->Width2 = 10; break;  
        case 2048: pic->Width2 = 11; break;  
        default: free(pic); return;  
    }  
  
    // Calcular Height2  
    switch(pic->Height) {  
        case   2: pic->Height2 = 1; break;  
        case   4: pic->Height2 = 2; break;  
        case   8: pic->Height2 = 3; break;  
        case  16: pic->Height2 = 4; break;  
        case  32: pic->Height2 = 5; break;  
        case  64: pic->Height2 = 6; break;  
        case 128: pic->Height2 = 7; break;  
        case 256: pic->Height2 = 8; break;  
        case 512: pic->Height2 = 9; break;  
        case 1024: pic->Height2 = 10; break;  
        case 2048: pic->Height2 = 11; break;  
        default: free(pic); return;  
    }  
  
    wld_pics[wld_num_pics++] = pic;  
    ptc->pPic = pic;  
}



void wld_load_pic(WLD_PicInfo *pic)  
{  
    if (!pic || pic->Raw) return;  
  
    GRAPH *tex_graph = get_tex_image(pic->code);  
    if (!tex_graph) return;  
  
    int size = pic->Width * pic->Height * 4; // 4 bytes por pixel (RGBA)  
    pic->Raw = malloc(size);  
    if (!pic->Raw) return;  
  
    // Copiar datos RGBA directamente sin conversión de paleta  
    for (int y = 0; y < pic->Height; y++) {  
        for (int x = 0; x < pic->Width; x++) {  
            uint32_t pixel = gr_get_pixel(tex_graph, x, y);  
            int idx = (y * pic->Width + x) * 4;  
            pic->Raw[idx] = (pixel >> 16) & 0xFF;     // R  
            pic->Raw[idx + 1] = (pixel >> 8) & 0xFF;  // G  
            pic->Raw[idx + 2] = pixel & 0xFF;         // B  
            pic->Raw[idx + 3] = (pixel >> 24) & 0xFF; // A  
        }  
    }  
  
    pic->Used++;  
}

int load_wld_standalone(const char *filename)  
{  
    FILE *f = fopen(filename, "rb");  
    if (!f) return 0;  
  
    fseek(f, 0, SEEK_END);  
    int size = ftell(f);  
    fseek(f, 0, SEEK_SET);  
  
    char *buffer = malloc(size);  
    if (!buffer) {  
        fclose(f);  
        return 0;  
    }  
  
    fread(buffer, 1, size, f);  
    fclose(f);  
  
    // Validar header WLD  
    if (memcmp(buffer, "wld\x1a\x0d\x0a\x01", 8) != 0) {  
        free(buffer);  
        return 0;  
    }  
  
    // Procesar geometría (adaptado de LoadZone)  
    int pos = 8 + 4 + *(int*)&buffer[8];  
      
    // Leer header principal  
    int num_points = *(int*)&buffer[pos + 8];  
    int num_regions = *(int*)&buffer[pos + 12];  
    int num_walls = *(int*)&buffer[pos + 16];  
  
    printf("WLD cargado: %d puntos, %d regiones, %d paredes\n",   
           num_points, num_regions, num_walls);  
  
    // Aquí procesarías la geometría según tus necesidades  
    // Por ahora solo cargamos las texturas referenciadas  
  
    free(buffer);  
    return 1;  
}

int64_t libmod_wld_load_wld(INSTANCE *my, int64_t *params)        
{        
    const char *filename = string_get(params[0]);        
    int fpg_id = params[1];          
          
    // Verificar solo error real (-1)      
    if (fpg_id == -1) {        
        printf("ERROR: No se pudo cargar el FPG\n");        
        string_discard(params[0]);        
        return 0;        
    }          
          
    // Verificar si el FPG existe usando grlib_get (API C interna)      
    if (!grlib_get(fpg_id)) {      
        printf("ERROR: El fpg_id %d no existe\n", fpg_id);        
        string_discard(params[0]);        
        return 0;        
    }          
          
    // Guardar fpg_id para uso en get_tex_image        
    wld_fpg_id = fpg_id;          
          
    FILE *fichero = fopen(filename, "rb");        
    if (!fichero) {        
        printf("ERROR: No se puede abrir '%s'\n", filename);        
        string_discard(params[0]);        
        return 0;        
    }          
          
    // INICIALIZAR MAPA ANTES DE USAR        
    memset(&wld_map, 0, sizeof(WLD_Map));          
          
    // Asignar arrays de punteros        
    wld_map.points = calloc(MAX_POINTS, sizeof(WLD_Point*));        
    wld_map.walls = calloc(MAX_WALLS, sizeof(WLD_Wall*));        
    wld_map.regions = calloc(MAX_REGIONS, sizeof(WLD_Region*));        
    wld_map.flags = calloc(MAX_FLAGS, sizeof(WLD_Flag*));          
          
    // Procesar geometría        
    if (!wld_process_geometry(fichero, &wld_map)) {        
        printf("ERROR: Fallo al procesar geometría\n");        
        fclose(fichero);        
        string_discard(params[0]);        
        return 0;        
    }          
          
    fclose(fichero);        
    wld_map.loaded = 1;         
      
    printf("WLD cargado exitosamente:\n");        
    printf("  - Puntos: %d\n", wld_map.num_points);        
    printf("  - Paredes: %d\n", wld_map.num_walls);        
      
    // SOLO construir punteros optimizados - SIN asignación de regiones estática  
    wld_build_wall_ptrs(&wld_map); 
    wld_assign_regions_simple(&wld_map, -1); 
    wld_calculate_nested_regions(&wld_map);
    wld_build_sectors(&wld_map);
    validate_and_fix_portals(&wld_map);
    debug_current_portals(&wld_map);
    analyze_wall_coordinates(&wld_map);  
    export_map_to_text(&wld_map, "map_debug.txt"); 
    printf("DEBUG: Mapa cargado - asignación de regiones será dinámica\n");      
    // debug_missing_textures(&wld_map); 
    // debug_wall_types(&wld_map);    
    // debug_find_portal_regions(&wld_map);    
    string_discard(params[0]);        
    return 1;        
}

int wld_process_geometry(FILE *fichero, WLD_Map *map)  
{  
    int i;  
    char cwork[9];  
    int total;  
        
    // Leer y verificar magic header  
    fread(cwork, 8, 1, fichero);  
    if (strcmp(cwork, "wld\x1a\x0d\x0a\x01\x00"))  
    {  
        printf("ERROR: No es un archivo WLD válido\n");  
        return 0;  
    }  
        
    // Leer total size  
    fread(&total, 4, 1, fichero);  
        
    // Saltar metadata del editor (548 bytes)  
    fseek(fichero, 548, SEEK_CUR);  
        
    // Leer puntos - CÓDIGO ORIGINAL QUE FUNCIONA  
    fread(&map->num_points, 4, 1, fichero);  
    printf("DEBUG: Leyendo %d puntos\n", map->num_points);  
    for (i = 0; i < map->num_points; i++) {  
        map->points[i] = malloc(sizeof(WLD_Point));  
        fread(map->points[i], sizeof(WLD_Point), 1, fichero);  
    }  
        
    // Leer paredes - CÓDIGO ORIGINAL QUE FUNCIONA  
    fread(&map->num_walls, 4, 1, fichero);  
    printf("DEBUG: Leyendo %d paredes\n", map->num_walls);  
    for (i = 0; i < map->num_walls; i++) {  
        map->walls[i] = malloc(sizeof(WLD_Wall));  
        fread(map->walls[i], sizeof(WLD_Wall), 1, fichero);  // ESTO CARGA TEXTURAS BIEN  
    }  
        
    // Leer regiones - CÓDIGO ORIGINAL CON FILTRADO AÑADIDO  
    fread(&map->num_regions, 4, 1, fichero);  
    // printf("DEBUG: Leyendo %d regiones\n", map->num_regions);  
    for (i = 0; i < map->num_regions; i++) {  
        map->regions[i] = malloc(sizeof(WLD_Region));  
        fread(map->regions[i], sizeof(WLD_Region), 1, fichero);  
          
        // AÑADIR: Filtrar regiones inválidas (-1, -1)  
        if (map->regions[i]->floor_height == -1 && map->regions[i]->ceil_height == -1) {  
            map->regions[i]->active = 0;  // Marcar como inactiva  
        } else {  
            map->regions[i]->active = 1;  // Región válida  
        }  
          
        // printf("DEBUG: Region[%d] - floor: %d, ceil: %d %s\n",   
        //        i, map->regions[i]->floor_height, map->regions[i]->ceil_height,  
        //        map->regions[i]->active ? "" : "(INACTIVA)");  
    }  
        
    // Leer flags  
    fread(&map->num_flags, 4, 1, fichero);  
    printf("DEBUG: Leyendo %d flags\n", map->num_flags);  
    for (i = 0; i < map->num_flags; i++) {  
        map->flags[i] = malloc(sizeof(WLD_Flag));  
        fread(map->flags[i], sizeof(WLD_Flag), 1, fichero);  
    }  
        
    // Leer skybox  
    fread(&map->skybox_angle, 4, 1, fichero);  
    sprintf(map->skybox_texture, "%d", map->skybox_angle);  
    map->skybox_angle = 120;  
        
    printf("DEBUG: Geometría WLD procesada correctamente\n");  
    return 1;  
}


void wld_debug_walls_with_x_diff(WLD_Map *map)  
{  
    int paredes_con_x_diferente = 0;  
    int paredes_x_cero = 0;  
      
    printf("DEBUG: Analizando paredes que usan puntos X≠0\n");  
      
    for (int i = 0; i < map->num_walls; i++) {  
        if (map->walls[i]->active != 0) {  
            WLD_Point *p1 = map->points[map->walls[i]->p1];  
            WLD_Point *p2 = map->points[map->walls[i]->p2];  
              
            if (p1->x != 0 || p2->x != 0) {  
                paredes_con_x_diferente++;  
                if (paredes_con_x_diferente <= 10) {  
                    // printf("DEBUG: pared X≠0[%d] - p1:%d(%d,%d), p2:%d(%d,%d)\n",   
                    //        i, map->walls[i]->p1, p1->x, p1->y,  
                    //        map->walls[i]->p2, p2->x, p2->y);  
                    //        fflush(stdout); 
                }  
            } else {  
                paredes_x_cero++;  
            }  
        }  
    }  
      
    // printf("DEBUG: Total paredes X≠0: %d, paredes X=0: %d\n",   
    //        paredes_con_x_diferente, paredes_x_cero);  
    //        fflush(stdout); 
}  
  
void wld_analyze_x_distribution(WLD_Map *map)  
{  
    int min_x = INT_MAX, max_x = INT_MIN;  
    int puntos_con_x_cero = 0;  
    int puntos_con_x_diferente = 0;  
      
    printf("DEBUG: Analizando distribución de coordenadas X\n");  
      
    for (int i = 0; i < map->num_points; i++) {  
        if (map->points[i]->active != 0) {  // Usar -> en lugar de .  
            if (map->points[i]->x < min_x) min_x = map->points[i]->x;  // Usar ->  
            if (map->points[i]->x > max_x) max_x = map->points[i]->x;  // Usar ->  
              
            if (map->points[i]->x == 0) {  // Usar ->  
                puntos_con_x_cero++;  
            } else {  
                puntos_con_x_diferente++;  
                if (puntos_con_x_diferente <= 10) {  
                    printf("DEBUG: punto con X≠0 [%d]: x=%d, y=%d\n",   
                           i, map->points[i]->x, map->points[i]->y);  // Usar ->  
                           fflush(stdout); 
                }  
            }  
        }  
    }  
      
    printf("DEBUG: Estadísticas X - min:%d, max:%d\n", min_x, max_x);  
    printf("DEBUG: Puntos con X=0: %d, con X≠0: %d\n", puntos_con_x_cero, puntos_con_x_diferente);  
}


void wld_render_2d(WLD_Map *map, int screen_w, int screen_h)  
{  
    if (!map || !map->loaded) {  
        printf("DEBUG: wld_render_2d - mapa no cargado o nulo\n");  
        return;  
    }  
      
    printf("DEBUG: Iniciando renderizado 2D - pantalla: %dx%d\n", screen_w, screen_h);  
      
    // Usar el render_buffer global  
    if (!render_buffer || render_buffer->width != screen_w || render_buffer->height != screen_h) {  
        if (render_buffer) bitmap_destroy(render_buffer);  
        render_buffer = bitmap_new_syslib(screen_w, screen_h);  
        if (!render_buffer) {  
            printf("ERROR: No se pudo crear render_buffer\n");  
            return;  
        }  
    }  
      
    // Limpiar pantalla  
    gr_clear_as(render_buffer, 0x404040);  
      
    // Analizar rango de coordenadas para ajustar escala automáticamente  
    int min_x = INT_MAX, max_x = INT_MIN;  
    int min_y = INT_MAX, max_y = INT_MIN;  
      
    for (int i = 0; i < map->num_points; i++) {  
        if (!map->points[i]) continue;  
        if (map->points[i]->x < min_x) min_x = map->points[i]->x;  
        if (map->points[i]->x > max_x) max_x = map->points[i]->x;  
        if (map->points[i]->y < min_y) min_y = map->points[i]->y;  
        if (map->points[i]->y > max_y) max_y = map->points[i]->y;  
    }  
    printf("DEBUG: Rango de coordenadas - X:[%d,%d], Y:[%d,%d]\n", min_x, max_x, min_y, max_y);  
    fflush(stdout); 
      
    // Calcular escala basada en el tamaño del mapa  
    float map_width = (float)(max_x - min_x);  
    float map_height = (float)(max_y - min_y);  
      
    if (map_width <= 0 || map_height <= 0) {  
        printf("ERROR: Dimensiones del mapa inválidas\n");  
        fflush(stdout); 
        return;  
    }  
      
    float scale_x = (float)screen_w / map_width * 0.8f;  
    float scale_y = (float)screen_h / map_height * 0.8f;  
    float scale = (scale_x < scale_y) ? scale_x : scale_y;  
      
    if (scale < 0.001f) scale = 0.001f;  
      
    // Calcular offset para centrar el mapa  
    int offset_x = screen_w / 2 - (int)((min_x + max_x) * scale / 2);  
    int offset_y = screen_h / 2 - (int)((min_y + max_y) * scale / 2);  
      
    printf("DEBUG: Usando escala: %.3f, offset X:%d, Y:%d\n", scale, offset_x, offset_y);  
    fflush(stdout); 
      
    int paredes_dibujadas = 0;  
    int fuera_de_pantalla = 0;  
    int indices_invalidos = 0;  
    int punteros_nulos = 0;  
      
    // Dibujar paredes usando algoritmo de línea simple  
    for (int i = 0; i < map->num_walls; i++) {  
        if (!map->walls[i]) {  
            punteros_nulos++;  
            continue;  
        }  
          
        int p1 = map->walls[i]->p1;  
        int p2 = map->walls[i]->p2;  
          
        // Validar índices  
        if (p1 < 0 || p1 >= map->num_points || p2 < 0 || p2 >= map->num_points) {  
            indices_invalidos++;  
            continue;  
        }  
          
        // Verificar punteros de puntos  
        if (!map->points[p1] || !map->points[p2]) {  
            punteros_nulos++;  
            continue;  
        }  
          
        // Transformar coordenadas  
        int x1 = (int)(map->points[p1]->x * scale) + offset_x;  
        int y1 = (int)(map->points[p1]->y * scale) + offset_y;  
        int x2 = (int)(map->points[p2]->x * scale) + offset_x;  
        int y2 = (int)(map->points[p2]->y * scale) + offset_y;  
          
        // Solo dibujar si ambos puntos están en pantalla  
        if (x1 >= 0 && x1 < screen_w && y1 >= 0 && y1 < screen_h &&  
            x2 >= 0 && x2 < screen_w && y2 >= 0 && y2 < screen_h) {  
              
            // Algoritmo de línea de Bresenham simplificado  
            int dx = abs(x2 - x1);  
            int dy = abs(y2 - y1);  
            int sx = (x1 < x2) ? 1 : -1;  
            int sy = (y1 < y2) ? 1 : -1;  
            int err = dx - dy;  
              
            while (1) {  
                if (x1 >= 0 && x1 < screen_w && y1 >= 0 && y1 < screen_h) {  
                    gr_put_pixel(render_buffer, x1, y1, 0xFFFFFF);  
                }  
                  
                if (x1 == x2 && y1 == y2) break;  
                  
                int e2 = 2 * err;  
                if (e2 > -dy) {  
                    err -= dy;  
                    x1 += sx;  
                }  
                if (e2 < dx) {  
                    err += dx;  
                    y1 += sy;  
                }  
            }  
              
            paredes_dibujadas++;  
              
            if (paredes_dibujadas <= 5) {  
                // printf("DEBUG: pared[%d] [%d,%d]->[%d,%d] -> pantalla [%d,%d]->[%d,%d]\n",  
                //        i, map->points[p1]->x, map->points[p1]->y,     
                //        map->points[p2]->x, map->points[p2]->y,  
                //        x1, y1, x2, y2);  
                //        fflush(stdout); 
            }  
        } else {  
            fuera_de_pantalla++;  
        }  
    }  
      
    printf("DEBUG: Paredes dibujadas: %d, fuera de pantalla: %d, índices inválidos: %d, punteros nulos: %d\n",     
           paredes_dibujadas, fuera_de_pantalla, indices_invalidos, punteros_nulos);  
           fflush(stdout); 
}
  
// Función de renderizado 2D  
int64_t libmod_wld_render_wld_2d(INSTANCE *my, int64_t *params)  
{  
    int width = params[0];  
    int height = params[1];  
      
    if (!wld_map.loaded) {  
        printf("ERROR: No hay mapa WLD cargado\n");  
        return 0;  
    }  
      
    wld_render_2d(&wld_map, width, height);  
      
    // Devolver el código del render_buffer global  
    return render_buffer ? render_buffer->code : 0;  
}

int64_t libmod_wld_test_render_buffer(INSTANCE *my, int64_t *params)  
{  
    int width = params[0];  
    int height = params[1];  
      
    if (!render_buffer || render_buffer->width != width || render_buffer->height != height) {  
        if (render_buffer) bitmap_destroy(render_buffer);  
        render_buffer = bitmap_new_syslib(width, height);  
        if (!render_buffer) return 0;  
    }  
      
    // Dibujar patrón de prueba  
    for (int y = 0; y < height; y++) {  
        for (int x = 0; x < width; x++) {  
            uint32_t color = ((x * 255) / width) | (((y * 255) / height) << 8);  
            gr_put_pixel(render_buffer, x, y, color);  
        }  
    }  
      
    printf("DEBUG: Patrón de prueba dibujado en %dx%d\n", width, height);  
    return render_buffer->code;  
}

// Función auxiliar para verificar si punto está en región
// Caché espacial para point_in_region()  
static int last_region_x = -999999, last_region_y = -999999;  
static int last_region_result = -1;  
static int last_region_cache[512];  
static int last_region_cache_size = 0;  
  
// Variables globales para el mapa  
static WLD_Map wld_map;  
static int map_loaded = 0;  

// NUEVO: Caché de regiones cercanas (calculado una vez por frame)
static int nearby_regions_cache[256];
static int nearby_regions_count = 0;
static float last_cache_x = -99999.0f;
static float last_cache_y = -99999.0f;
  
int point_in_region(float x, float y, int region_idx, WLD_Map *map)  
{  
    if (!map || region_idx < 0 || region_idx >= map->num_regions) {  
        return 0;  
    }  
      
    // Usar estructura de optimización en lugar de la original  
    WLD_Region_Optimized *opt_region = &optimized_regions[region_idx];  
    int inside = 0;  
      
    // OPTIMIZACIÓN: Usar WallPtrs desde la estructura optimizada  
    for (int i = 0; i < opt_region->num_wall_ptrs; i++) {  
        WLD_Wall *wall = opt_region->wall_ptrs[i];  
          
        int p1 = wall->p1;  
        int p2 = wall->p2;  
        if (!map->points[p1] || !map->points[p2]) continue;  
          
        float x1 = map->points[p1]->x;  
        float y1 = map->points[p1]->y;  
        float x2 = map->points[p2]->x;  
        float y2 = map->points[p2]->y;  
          
        // Ray casting algorithm  
        if (((y1 > y) != (y2 > y)) &&  
            (x < (x2 - x1) * (y - y1) / (y2 - y1) + x1)) {  
            inside = !inside;  
        }  
    }  
    return inside;  
}
  
// Función para escanear paredes visibles (similar a ScanRegion de VPE)  
void scan_walls_from_region(WLD_Map *map, int region_idx, float cam_x, float cam_y,  
                           float ray_dir_x, float ray_dir_y, float *hit_distance,  
                           WLD_Wall **hit_wall, int *hit_region, int *adjacent_region)  
{  
    int visited_regions[256];  
    int num_visited = 0;  
    int regions_to_visit[256];  
    regions_to_visit[0] = region_idx;  
    int num_to_visit = 1;
      
    *hit_distance = 999999.0f;  
    *hit_wall = NULL;  
    *hit_region = region_idx;  
    *adjacent_region = -1;  
      
    while (num_to_visit > 0) {  
        int current = regions_to_visit[--num_to_visit];  // LIFO: tomar del final
          
        // Verificar si ya visitamos  
        int already_visited = 0;  
        for (int i = 0; i < num_visited; i++) {  
            if (visited_regions[i] == current) {  
                already_visited = 1;  
                break;  
            }  
        }  
        if (already_visited) continue;  
        visited_regions[num_visited++] = current;  
          
        // Añadir regiones anidadas pre-calculadas (ahora con FIFO)
        WLD_Region_Optimized *opt_current = &optimized_regions[current];
        for (int i = 0; i < opt_current->num_nested_regions; i++) {
            int nested = opt_current->nested_regions[i];
            
            int already_visited_nested = 0;
            for (int j = 0; j < num_visited; j++) {
                if (visited_regions[j] == nested) {
                    already_visited_nested = 1;
                    break;
                }
            }
            
            if (!already_visited_nested && num_to_visit < 256) {
                regions_to_visit[num_to_visit++] = nested;
            }
        }
          
        // Usar estructura optimizada  
        WLD_Region_Optimized *opt_region = &optimized_regions[current];  
          
        for (int i = 0; i < opt_region->num_wall_ptrs; i++) {  
            WLD_Wall *wall = opt_region->wall_ptrs[i];  
            if (!wall) continue;  
              
            int front = wall->front_region;  
            int back = wall->back_region;  
              
            if (front != current && back != current) continue;  
              
            int p1 = wall->p1;  
            int p2 = wall->p2;  
            if (p1 < 0 || p1 >= map->num_points || p2 < 0 || p2 >= map->num_points) continue;  
            if (!map->points[p1] || !map->points[p2]) continue;  
              
            float x1 = map->points[p1]->x;  
            float y1 = map->points[p1]->y;  
            float x2 = map->points[p2]->x;  
            float y2 = map->points[p2]->y;  
              
            float t = intersect_ray_segment(ray_dir_x, ray_dir_y, cam_x, cam_y,  
                                           x1, y1, x2, y2);  
              
            if (t > 0.001f && t < *hit_distance) {  
                *hit_distance = t;  
                *hit_wall = wall;  
                int adjacent = (front == current) ? back : front;  
                *adjacent_region = adjacent;  
                *hit_region = current;  
            }  
        }  
    }  
}

void render_floor_and_ceiling(WLD_Map *map, WLD_Region *region, int col,  
                                     int screen_w, int screen_h, int wall_top, int wall_bottom,  
                                     float cam_x, float cam_y, float cam_z, float distance,  
                                     float fog_factor, int clip_top, int clip_bottom, float angle_offset)  
{  
    if (!region) return;  
      
    // Precalcular coseno para corrección de fisheye
    float cos_angle = cos(angle_offset);
    if (cos_angle < 0.0001f) cos_angle = 0.0001f;

    // Renderizar techo (desde clip_top hasta wall_top)  
    int ceil_end = (wall_top < clip_bottom) ? wall_top : clip_bottom;
    int ceil_start = clip_top;
    
    if (ceil_start < ceil_end) {  
        GRAPH *ceil_tex = get_tex_image(region->ceil_tex);  
        if (ceil_tex) {  
            for (int y = ceil_start; y < ceil_end; y++) {  
                float y_diff = (float)y - (screen_h / 2.0f);  
                if (fabs(y_diff) < 0.1f) continue;  
                  
                float height_diff = cam_z - region->ceil_height;  
                // CORRECCIÓN FISHEYE: Usar distancia corregida
                float ceil_distance = (height_diff * wld_focal_length) / y_diff;  
                float corrected_distance = ceil_distance / cos_angle;
                  
                if (corrected_distance < 0.1f) continue;  
                  
                float hit_x = cam_x + cos(camera.angle + angle_offset) * corrected_distance;  
                float hit_y = cam_y + sin(camera.angle + angle_offset) * corrected_distance;  
                  
                int tex_x = ((int)(hit_x * 0.5f)) % ceil_tex->width;  
                int tex_y = ((int)(hit_y * 0.5f)) % ceil_tex->height;  
                  
                if (tex_x < 0) tex_x += ceil_tex->width;  
                if (tex_y < 0) tex_y += ceil_tex->height;  
                  
                uint32_t pixel = gr_get_pixel(ceil_tex, tex_x, tex_y);  
                uint8_t r = (pixel >> gPixelFormat->Rshift) & 0xFF;  
                uint8_t g = (pixel >> gPixelFormat->Gshift) & 0xFF;  
                uint8_t b = (pixel >> gPixelFormat->Bshift) & 0xFF;  
                  
                // Fog para techo
                float dist_factor = 1.0f - (corrected_distance / max_render_distance);
                if (dist_factor < 0.0f) dist_factor = 0.0f;
                r = (uint8_t)(r * dist_factor);
                g = (uint8_t)(g * dist_factor);
                b = (uint8_t)(b * dist_factor);
                  
                uint32_t color = SDL_MapRGB(gPixelFormat, r, g, b);  
                gr_put_pixel(render_buffer, col, y, color);  
            }  
        }  
    }  
      
    // Renderizar suelo (desde wall_bottom hasta clip_bottom)  
    int floor_start = (wall_bottom > clip_top) ? wall_bottom : clip_top;
    int floor_end = clip_bottom;
    
    if (floor_start < floor_end) {  
        GRAPH *floor_tex = get_tex_image(region->floor_tex);  
        if (floor_tex) {  
            for (int y = floor_start; y <= floor_end; y++) {  
                float y_diff = (float)y - (screen_h / 2.0f);  
                if (fabs(y_diff) < 0.1f) continue;  
                  
                float height_diff = cam_z - region->floor_height;  
                // CORRECCIÓN FISHEYE: Usar distancia corregida
                float floor_distance = (height_diff * wld_focal_length) / y_diff;  
                float corrected_distance = floor_distance / cos_angle;
                  
                if (corrected_distance < 0.1f) continue;  
                  
                float hit_x = cam_x + cos(camera.angle + angle_offset) * corrected_distance;  
                float hit_y = cam_y + sin(camera.angle + angle_offset) * corrected_distance;  
                  
                int tex_x = ((int)(hit_x * 0.5f)) % floor_tex->width;  
                int tex_y = ((int)(hit_y * 0.5f)) % floor_tex->height;  
                  
                if (tex_x < 0) tex_x += floor_tex->width;  
                if (tex_y < 0) tex_y += floor_tex->height;  
                  
                uint32_t pixel = gr_get_pixel(floor_tex, tex_x, tex_y);  
                uint8_t r = (pixel >> gPixelFormat->Rshift) & 0xFF;  
                uint8_t g = (pixel >> gPixelFormat->Gshift) & 0xFF;  
                uint8_t b = (pixel >> gPixelFormat->Bshift) & 0xFF;  
                  
                // Fog para suelo
                float dist_factor = 1.0f - (corrected_distance / max_render_distance);
                if (dist_factor < 0.0f) dist_factor = 0.0f;
                r = (uint8_t)(r * dist_factor);
                g = (uint8_t)(g * dist_factor);
                b = (uint8_t)(b * dist_factor);
                  
                uint32_t color = SDL_MapRGB(gPixelFormat, r, g, b);  
                gr_put_pixel(render_buffer, col, y, color);  
            }  
        }  
    }  
}

void render_wall_column(WLD_Map *map, WLD_Wall *wall, int region_idx,   
                              int col, int screen_w, int screen_h,  
                              float cam_x, float cam_y, float cam_z, float distance,
                              int clip_top, int clip_bottom)
{
    if (!wall || region_idx < 0 || region_idx >= map->num_regions) return;
    
    WLD_Region *region = map->regions[region_idx];
    if (!region) return;
    
    // CORRECCIÓN FISHEYE: Usar distancia corregida para la proyección vertical
    float angle_offset = ((float)col - screen_w/2.0f) * wld_angle_step;
    float corrected_distance = distance * cos(angle_offset);
    if (corrected_distance < 0.1f) corrected_distance = 0.1f;

    // Calcular altura proyectada usando distancia corregida
    float t1 = wld_focal_length / corrected_distance;
    float t_floor = (cam_z - region->floor_height);
    float t_ceil = (cam_z - region->ceil_height);
    
    int FBot = (screen_h/2.0f) + t_floor * t1;
    int FTop = (screen_h/2.0f) + t_ceil * t1;
    
    // Asegurar orden
    if (FTop > FBot) {
        int temp = FTop; FTop = FBot; FBot = temp;
    }
    
    // Guardar valores originales para suelo/techo
    int original_FTop = FTop;
    int original_FBot = FBot;
    float original_height = (float)(FBot - FTop);
    if (original_height < 1.0f) original_height = 1.0f;
    
    // Clampear a la pantalla y al clipping window
    int draw_top = (FTop < clip_top) ? clip_top : FTop;
    int draw_bot = (FBot > clip_bottom) ? clip_bottom : FBot;
    
    // Renderizar pared si es visible
    if (draw_top < draw_bot) {
        // Calcular wall_u
        int p1 = wall->p1;
        int p2 = wall->p2;
        float wall_u = 0.0f;
        if (p1 >= 0 && p1 < map->num_points && p2 >= 0 && p2 < map->num_points) {
            float x1 = map->points[p1]->x;
            float y1 = map->points[p1]->y;
            float x2 = map->points[p2]->x;
            float y2 = map->points[p2]->y;
            
            // angle_offset ya calculado arriba
            float ray_dir_x = cos(camera.angle + angle_offset);
            float ray_dir_y = sin(camera.angle + angle_offset);
            
            float hit_x = cam_x + ray_dir_x * distance;
            float hit_y = cam_y + ray_dir_y * distance;
            
            float wall_dx = x2 - x1;
            float wall_dy = y2 - y1;
            float wall_len = sqrtf(wall_dx * wall_dx + wall_dy * wall_dy);
            
            if (wall_len > 0.001f) {
                float t = ((hit_x - x1) * wall_dx + (hit_y - y1) * wall_dy) / (wall_len * wall_len);
                wall_u = (t < 0.0f) ? 0.0f : (t > 1.0f) ? 1.0f : t;
            }
        }
        
        float fog_factor = 1.0f - (distance / max_render_distance);
        if (fog_factor < 0.0f) fog_factor = 0.0f;
        
        render_wall_section(map, wall->texture, col, draw_top, draw_bot, wall_u, fog_factor, "WALL", (float)FTop, original_height);
    }
    
    // Renderizar suelo y techo
    // Usamos los clips originales pero limitados por la pared dibujada para que coincidan
    // O mejor, pasamos los clips actuales y dejamos que render_floor_and_ceiling se encargue
    // Pero render_floor_and_ceiling necesita saber dónde termina la pared para empezar a dibujar
    
    // Si la pared fue clippeada totalmente, aún necesitamos dibujar suelo/techo en el espacio visible
    int eff_top = (original_FTop < clip_top) ? clip_top : (original_FTop > clip_bottom) ? clip_bottom : original_FTop;
    int eff_bot = (original_FBot < clip_top) ? clip_top : (original_FBot > clip_bottom) ? clip_bottom : original_FBot;
    
    render_floor_and_ceiling(map, region, col, screen_w, screen_h, eff_top, eff_bot, 
                            cam_x, cam_y, cam_z, distance, 1.0f, clip_top, clip_bottom, angle_offset);
}

// Función para calcular distancia entre dos puntos  
static float distance_between_points(int32_t x1, int32_t y1, int32_t x2, int32_t y2) {  
    float dx = (float)(x2 - x1);  
    float dy = (float)(y2 - y1);  
    return sqrtf(dx * dx + dy * dy);  
}  
  
// Función para encontrar la distancia mínima entre dos segmentos  
static float min_distance_between_segments(  
    int32_t ax1, int32_t ay1, int32_t ax2, int32_t ay2,  
    int32_t bx1, int32_t by1, int32_t bx2, int32_t by2) {  
      
    // Probar múltiples puntos en ambos segmentos  
    float min_dist = 999999.0f;  
      
    for (float t = 0.0f; t <= 1.0f; t += 0.25f) {  
        float ax = ax1 + (ax2 - ax1) * t;  
        float ay = ay1 + (ay2 - ay1) * t;  
          
        for (float s = 0.0f; s <= 1.0f; s += 0.25f) {  
            float bx = bx1 + (bx2 - bx1) * s;  
            float by = by1 + (by2 - by1) * s;  
              
            float dist = distance_between_points((int32_t)ax, (int32_t)ay, (int32_t)bx, (int32_t)by);  
            if (dist < min_dist) min_dist = dist;  
        }  
    }  
      
    return min_dist;  
}

void render_wld(WLD_Map *map, int screen_w, int screen_h)    
{    
    if (!map || !map->loaded) return;    
        
    // Crear buffer si es necesario    
    if (!render_buffer || render_buffer->width != screen_w || render_buffer->height != screen_h) {    
        if (render_buffer) bitmap_destroy(render_buffer);    
        render_buffer = bitmap_new_syslib(screen_w, screen_h);    
        if (!render_buffer) return;    
    }    
        
    // Skybox    
    uint32_t sky_color = SDL_MapRGBA(gPixelFormat, sky_color_r, sky_color_g, sky_color_b, sky_color_a);    
    gr_clear_as(render_buffer, sky_color);    
        
    // Encontrar región actual    
    int current_region = -1;    
    for (int i = 0; i < map->num_regions; i++) {    
        if (map->regions[i] && map->regions[i]->active &&          
            point_in_region(camera.x, camera.y, i, map)) {    
            current_region = i;    
            break;    
        }    
    }    
        
    if (current_region < 0) {    
        printf("DEBUG: Cámara fuera de todas las regiones válidas\n");    
        return;    
    }    
        
    // Renderizar cada columna con raycasting continuo    
    for (int col = 0; col < screen_w; col++) {    
        float angle_offset = ((float)col - screen_w/2.0f) * wld_angle_step;  // Usar wld_angle_step    
        float ray_dir_x = cos(camera.angle + angle_offset);    
        float ray_dir_y = sin(camera.angle + angle_offset);    
            
        // Inicializar clipping vertical para esta columna  
        int clip_top = 0;  
        int clip_bottom = screen_h - 1;  
          
        // Raycasting continuo a través de portales    
        float total_distance = 0.0f;    
        int current_sector = current_region;    
        int max_depth = 16; // Aumentado para permitir más profundidad  
        int depth = 0;    
            
        while (depth < max_depth && total_distance < max_render_distance) {    
            WLD_Wall *hit_wall;    
            int hit_region, adjacent_region;    
            float hit_distance;    
                
            scan_walls_from_region(map, current_sector,       
                                  camera.x + ray_dir_x * total_distance,    
                                  camera.y + ray_dir_y * total_distance,    
                                  ray_dir_x, ray_dir_y, &hit_distance,    
                                  &hit_wall, &hit_region, &adjacent_region);    
                
            if (hit_wall && hit_distance < 999999.0f) {    
                total_distance += hit_distance;    
                
                // CORRECCIÓN FISHEYE: Usar distancia corregida para proyecciones
                float corrected_distance = total_distance * cos(angle_offset);
                if (corrected_distance < 0.1f) corrected_distance = 0.1f;
                    
                // NUEVO: Renderizar suelo y techo del sector actual  
                WLD_Region *current_sector_region = map->regions[current_sector];  
                if (current_sector_region && current_sector_region->active) {  
                    render_floor_and_ceiling(map, current_sector_region, col, screen_w, screen_h,  
                                            clip_top, clip_bottom, camera.x, camera.y, camera.z,  
                                            total_distance, 1.0f, clip_top, clip_bottom, angle_offset);  
                }
                
                // Renderizar regiones anidadas visibles en esta columna
                WLD_Region_Optimized *opt_sector = &optimized_regions[current_sector];
                for (int n = 0; n < opt_sector->num_nested_regions; n++) {
                    int nested_idx = opt_sector->nested_regions[n];
                    
                    // Lanzar rayo para ver si intersecta con esta región anidada
                    WLD_Wall *nested_wall;
                    int nested_hit_region, nested_adjacent;
                    float nested_distance;
                    
                    scan_walls_from_region(map, nested_idx,
                                          camera.x, camera.y,
                                          ray_dir_x, ray_dir_y,
                                          &nested_distance,
                                          &nested_wall, &nested_hit_region, &nested_adjacent);
                    
                    if (nested_wall && nested_distance < total_distance && nested_distance > 0.001f) {
                        // DEBUG: Solo para primera columna
                        if (col == screen_w / 2) {
                            printf("DEBUG col %d: Nested region %d found wall at distance %.2f (current: %.2f)\n",
                                   col, nested_idx, nested_distance, total_distance);
                        }
                        
                        // Esta región anidada está más cerca que la pared actual
                        float nested_corrected = nested_distance * cos(angle_offset);
                        if (nested_corrected < 0.1f) nested_corrected = 0.1f;
                        
                        render_wall_column(map, nested_wall, nested_hit_region, col,
                                          screen_w, screen_h, camera.x, camera.y, camera.z,
                                          nested_distance, clip_top, clip_bottom);
                    } else if (col == screen_w / 2 && opt_sector->num_nested_regions > 0) {
                        // DEBUG: Ver por qué no encuentra
                        printf("DEBUG col %d: Nested region %d - wall=%p, dist=%.2f, total=%.2f\n",
                               col, nested_idx, (void*)nested_wall, nested_distance, total_distance);
                    }
                }
                    
                // Si es pared sólida, renderizar y terminar    
                if (adjacent_region == -1) {    
                    render_wall_column(map, hit_wall, hit_region, col, screen_w, screen_h,    
                                      camera.x, camera.y, camera.z, total_distance, clip_top, clip_bottom);    
                    break;    
                }    
                    
                // Si es portal, verificar si tiene geometría compleja    
                bool is_complex = false;    
                if (adjacent_region >= 0 && adjacent_region < map->num_regions) {    
                    WLD_Region *current = map->regions[hit_region];    
                    WLD_Region *adjacent = map->regions[adjacent_region];    
                        
                    if (current && adjacent && current->active && adjacent->active) {    
                        // Siempre tratamos como complejo para manejar el clipping correctamente  
                        is_complex = true;    
                    }    
                }    
                    
                // Renderizar portal complejo Y CONTINUAR    
                if (is_complex) {    
                    // Calcular wall_u y fog    
                    int p1 = hit_wall->p1;    
                    int p2 = hit_wall->p2;    
                    float wall_u = 0.0f;    
                    if (p1 >= 0 && p1 < map->num_points && p2 >= 0 && p2 < map->num_points) {    
                        float x1 = map->points[p1]->x;    
                        float y1 = map->points[p1]->y;    
                        float x2 = map->points[p2]->x;    
                        float y2 = map->points[p2]->y;    
                            
                        float hit_x = camera.x + ray_dir_x * total_distance;    
                        float hit_y = camera.y + ray_dir_y * total_distance;    
                            
                        float wall_dx = x2 - x1;    
                        float wall_dy = y2 - y1;    
                        float wall_len = sqrtf(wall_dx * wall_dx + wall_dy * wall_dy);    
                            
                        if (wall_len > 0.001f) {    
                            float t = ((hit_x - x1) * wall_dx + (hit_y - y1) * wall_dy) / (wall_len * wall_len);    
                            wall_u = (t < 0.0f) ? 0.0f : (t > 1.0f) ? 1.0f : t;    
                        }    
                    }    
                        
                    float fog_factor = 1.0f - (total_distance / (max_render_distance * 2.0f));    
                    if (fog_factor < 0.3f) fog_factor = 0.3f;    
                    if (fog_factor > 1.0f) fog_factor = 1.0f;    
                        
                    // Renderizar con los clips actuales  
                    render_complex_wall_section(map, hit_wall, map->regions[hit_region],         
                                               hit_region, col, screen_w, screen_h,    
                                               0, screen_h, wall_u, fog_factor,    
                                               camera.x, camera.y, camera.z, total_distance, clip_top, clip_bottom);    
                        
                    // CALCULAR NUEVOS CLIPS PARA EL SIGUIENTE SECTOR  
                    WLD_Region *current = map->regions[hit_region];  
                    WLD_Region *adjacent = map->regions[adjacent_region];  
                      
                    // Determinar la apertura del portal (intersección de rangos verticales)  
                    // El suelo más alto y el techo más bajo definen la apertura  
                    int open_floor = (current->floor_height > adjacent->floor_height) ? current->floor_height : adjacent->floor_height;  
                    int open_ceil = (current->ceil_height < adjacent->ceil_height) ? current->ceil_height : adjacent->ceil_height;  
                    
                    // FIX: Si el portal está cerrado (suelo >= techo), no dibujar nada a través de él
                    if (open_floor >= open_ceil) {
                        break; // Detener renderizado de esta columna (pared sólida visualmente)
                    }
                      
                    // Proyectar a pantalla usando distancia corregida
                    float t1 = wld_focal_length / corrected_distance;  
                      
                    float t_floor = (camera.z - open_floor);  
                    float t_ceil = (camera.z - open_ceil);  
                      
                    int screen_floor = (screen_h/2.0f) + t_floor * t1;  
                    int screen_ceil = (screen_h/2.0f) + t_ceil * t1;  
                      
                    // Asegurar orden (techo arriba, suelo abajo) - YA NO ES NECESARIO SWAP PORQUE VALIDAMOS ARRIBA
                    // Pero mantenemos la lógica de renderizado normal
                      
                    // Clampear a pantalla  
                    if (screen_ceil < 0) screen_ceil = 0;  
                    if (screen_floor >= screen_h) screen_floor = screen_h - 1;  
                      
                    // Actualizar clips (intersección con ventana actual)  
                    // IMPORTANTE: Para evitar líneas azules, debemos asegurarnos de que los clips se actualicen
                    // exactamente donde termina la pared dibujada.
                    // render_complex_wall_section ya dibuja hasta open_ceil y desde open_floor si es necesario.
                    
                    // Ajustar clips para el siguiente sector
                    // Usamos ceil/floor para definir la nueva ventana
                    if (screen_ceil > clip_top) clip_top = screen_ceil;  
                    if (screen_floor < clip_bottom) clip_bottom = screen_floor;
                    
                    // CORRECCIÓN LÍNEAS AZULES:
                    // Si hay un gap de 1 pixel debido al redondeo, lo cerramos expandiendo el clip 1 pixel
                    // Esto asume que render_complex_wall_section dibujó "de más" o "de menos" por un pixel
                    // Pero es más seguro asegurar que la ventana se cierra exactamente donde empieza el hueco.
                    
                    // Forzar que los clips sean consistentes con lo que se dibujó
                    // (render_complex_wall_section usa lógica similar para dibujar top/bot)
                    
                    // Si el portal es muy estrecho, podría cerrarse
                    if (clip_top > clip_bottom) {
                        // Intentar recuperar 1 pixel si están adyacentes (caso borde)
                        if (clip_top == clip_bottom + 1) {
                             clip_top = clip_bottom; 
                        } else {
                             break;
                        }
                    }
                      
                    // Si la ventana se cierra, dejar de renderizar  
                    if (clip_top > clip_bottom) {  
                        break;  
                    }  
                        
                    // Continuar al siguiente sector  
                    current_sector = adjacent_region;    
                    depth++;    
                } else {    
                    // Portal simple (no debería ocurrir con la lógica actual, pero por seguridad)  
                    current_sector = adjacent_region;    
                    depth++;    
                }    
            } else {    
                break; // No hay más paredes    
            }    
        }    
    }    
}

int wld_find_region(WLD_Map *map, float x, float y, int discard_region)  
{  
  
      
    // Búsqueda simple: iterar regiones y usar ray casting  
    for (int i = 0; i < map->num_regions; i++) {  
        if (i == discard_region) continue;  
        if (!map->regions[i]) continue;  
          
        if (point_in_region(x, y, i, map)) {  
            return i;  
        }  
    }  
    return -1;  
}

void wld_sort_regions(WLD_Map *map)  
{  
    int i;  
      
    // Inicializar todas las regiones como tipo 1  
    for (i = 0; i < map->num_regions; i++) {  
        map->regions[i]->type = 1;  
    }  
}

// Función de intersección rayo-segmento  
float intersect_ray_segment(float ray_dir_x, float ray_dir_y, float cam_x, float cam_y,  
                           float seg_x1, float seg_y1, float seg_x2, float seg_y2)  
{  
    // Transformar segmento a coordenadas relativas a la cámara  
    float x1 = seg_x1 - cam_x;  
    float y1 = seg_y1 - cam_y;  
    float x2 = seg_x2 - cam_x;  
    float y2 = seg_y2 - cam_y;  
      
    float seg_dx = x2 - x1;  
    float seg_dy = y2 - y1;  
      
    float denominator = ray_dir_x * seg_dy - ray_dir_y * seg_dx;  
    if (fabs(denominator) < 0.0001f) return -1;  
      
    float t = (x1 * seg_dy - y1 * seg_dx) / denominator;  
    float s = (x1 * ray_dir_y - y1 * ray_dir_x) / denominator;  
      
    if (t > 0 && s >= 0 && s <= 1) {  
        return t;  
    }  
    return -1;  
}

// Función exportada para BennuGD2 - similar a libmod_wld_render_wld_2d()  
int64_t libmod_wld_render_wld_3d(INSTANCE *my, int64_t *params) {  
    int width = params[0];  
    int height = params[1];  
      
    // Si hay un tercer parámetro, es el FOV  
    if (params[2] != 0) {  
        float new_fov = *(float*)&params[2];  
        if (new_fov >= 0.001f && new_fov <= 0.01f) {  
            wld_angle_step = new_fov;  
            wld_focal_length = 0.9f / wld_angle_step;
        }  
    } else {
        // Si no hay parámetro explícito, usar camera.fov
        // camera.fov está en grados (ej: 60.0)
        // wld_angle_step es radianes por columna
        if (camera.fov > 0.0f && width > 0) {
             float fov_rad = camera.fov * (M_PI / 180.0f);
             wld_angle_step = fov_rad / (float)width;
             wld_focal_length = 0.9f / wld_angle_step;
        }
    }
      
    if (!wld_map.loaded) {  
        printf("ERROR: No hay mapa WLD cargado\n");  
        return 0;  
    }  
      
    render_wld(&wld_map, width, height);  
    return render_buffer ? render_buffer->code : 0;  
}


  
void wld_build_wall_ptrs(WLD_Map *map)  
{  
    int total_ptrs = 0;  
      
    // Contar referencias totales  
    for (int i = 0; i < map->num_walls; i++) {  
        if (map->walls[i]->front_region >= 0) total_ptrs++;  
        if (map->walls[i]->back_region >= 0) total_ptrs++;  
    }  
      
    // Asignar array contiguo  
    WLD_Wall **wall_ptrs = malloc(total_ptrs * sizeof(WLD_Wall*));  
    int ptr_index = 0;  
      
    // Construir punteros en estructura separada  
    for (int i = 0; i < map->num_regions; i++) {  
        optimized_regions[i].original_region = map->regions[i];  
        optimized_regions[i].wall_ptrs = &wall_ptrs[ptr_index];  
        optimized_regions[i].num_wall_ptrs = 0;  
          
        for (int j = 0; j < map->num_walls; j++) {  
            if (map->walls[j]->front_region == i || map->walls[j]->back_region == i) {  
                wall_ptrs[ptr_index++] = map->walls[j];  
                optimized_regions[i].num_wall_ptrs++;  
            }  
        }
        
        // Inicializar contador de sectores anidados a 0
        optimized_regions[i].num_nested_regions = 0;
    }
}  



// Añadir esta función para contar paredes portal vs sólidas  
void debug_wall_types(WLD_Map *map)  
{  
    int portal_walls = 0;  
    int solid_walls = 0;  
      
    for (int i = 0; i < map->num_walls; i++) {  
        if (map->walls[i]->front_region >= 0 && map->walls[i]->back_region >= 0) {  
            portal_walls++;  
        } else {  
            solid_walls++;  
        }  
    }  
      
    printf("DEBUG: Paredes portal: %d, Paredes sólidas: %d\n",   
           portal_walls, solid_walls);  
}  

void wld_assign_regions_simple(WLD_Map *map, int target_region)  
{  
    if (!map || !map->loaded) return;  
      
    printf("DEBUG: Asignando portales para todo el mapa...\n");  
      
    // Basado en map_asignregions() de DIV  
    for (int i = 0; i < map->num_walls; i++) {  
        WLD_Wall *wall = map->walls[i];  
        if (!wall) continue;  
          
        // Inicializar  
        wall->back_region = -1;  
        wall->type = 2; // Sólida por defecto  
          
        // Buscar pared compartida con vértices opuestos  
        for (int j = i + 1; j < map->num_walls; j++) {  
            WLD_Wall *wall2 = map->walls[j];  
            if (!wall2) continue;  
              
            // Misma pared, orientación opuesta = portal  
            if ((wall->p1 == wall2->p2 && wall->p2 == wall2->p1) &&  
                wall->front_region != wall2->front_region) {  
                  
                wall->back_region = wall2->front_region;  
                wall2->back_region = wall->front_region;  
                wall->type = 1; // Portal  
                wall2->type = 1;  
                  
                // Asignar texturas para geometría compleja  
                wall->texture_top = wall->texture;  
                wall->texture_bot = wall->texture;  
                wall2->texture_top = wall2->texture;  
                wall2->texture_bot = wall2->texture;  
            }  
        }  
    }  
      
    // Para paredes sin vértices compartidos, usar point-in-polygon  
    for (int i = 0; i < map->num_walls; i++) {  
        WLD_Wall *wall = map->walls[i];  
        if (!wall || wall->back_region >= 0) continue;  
          
        int p1 = wall->p1;  
        int p2 = wall->p2;  
        if (p1 < 0 || p2 < 0) continue;  
          
        float mid_x = (map->points[p1]->x + map->points[p2]->x) / 2.0f;  
        float mid_y = (map->points[p1]->y + map->points[p2]->y) / 2.0f;  
          
        int back_region = wld_find_region(map, mid_x, mid_y, wall->front_region);  
        if (back_region >= 0) {  
            wall->back_region = back_region;  
            wall->type = 1;  
            wall->texture_top = wall->texture;  
            wall->texture_bot = wall->texture;  
        }  
    }  
      
    // Verificar portales asignados  
    int total = 0;  
    for (int i = 0; i < map->num_walls; i++) {  
        if (map->walls[i] && map->walls[i]->back_region >= 0) {  
            total++;  
        }  
    }  
    printf("DEBUG: Total portales asignados: %d\n", total);  
}

void wld_clear_portal_assignments(WLD_Map *map)  
{  
    if (!map || !map->loaded) return;  
      
    for (int i = 0; i < map->num_walls; i++) {  
        if (map->walls[i]) {  
            map->walls[i]->back_region = -1;  
            map->walls[i]->type = 2; // Sólida  
            map->walls[i]->texture_top = 0;  
            map->walls[i]->texture_bot = 0;  
        }  
    }  
}

void debug_find_portal_regions(WLD_Map *map)  
{  
    printf("DEBUG: Regiones con paredes portal (mostrando primeras 20):\n");  
    int count = 0;  
      
    for (int region = 0; region < map->num_regions && count < 20; region++) {  
        int has_portal = 0;  
          
        for (int i = 0; i < map->num_walls; i++) {  
            if ((map->walls[i]->front_region == region || map->walls[i]->back_region == region) &&  
                map->walls[i]->front_region >= 0 && map->walls[i]->back_region >= 0) {  
                has_portal = 1;  
                break;  
            }  
        }  
          
        if (has_portal) {  
            printf("  Región %d (floor: %d, ceil: %d)\n",  
                   region, map->regions[region]->floor_height, map->regions[region]->ceil_height);  
            count++;  
        }  
    }  
      
    if (count >= 20) {  
        printf("  ... y más regiones con portales\n");  
    }  
}

void debug_missing_textures(WLD_Map *map)  
{  
    printf("DEBUG: Verificando texturas faltantes:\n");  
    for (int i = 0; i < map->num_walls; i++) {  
        WLD_Wall *wall = map->walls[i];  
        if (!wall) continue;  
          
        if (wall->type == 1) { // Portal  
            printf("  Pared[%d] (portal): texture=%d, top=%d, bot=%d\n",  
                   i, wall->texture, wall->texture_top, wall->texture_bot);  
        }  
    }  
      
    // Verificar texturas de regiones  
    for (int i = 0; i < map->num_regions; i++) {  
        WLD_Region *region = map->regions[i];  
        if (region && region->active) {  
            printf("  Región[%d]: floor_tex=%d, ceil_tex=%d\n",  
                   i, region->floor_tex, region->ceil_tex);  
        }  
    }  
}


void debug_current_portals(WLD_Map *map)  
{  
    printf("DEBUG: Portales en mapa cargado (mostrando primeros 20):\n");  
    int count = 0;  
      
    for (int i = 0; i < map->num_walls && count < 20; i++) {  
        WLD_Wall *wall = map->walls[i];  
        if (!wall) continue;  
          
        // Mostrar paredes que tienen back_region asignado (portales)  
        // if (wall->front_region >= 0 && wall->back_region >= 0) {  
        //     printf("  Pared[%d]: región %d -> %d (type=%d)\n",   
        //            i, wall->front_region, wall->back_region, wall->type);  
        //     count++;  
        // }  
    }  
      
    if (count >= 20) {  
        printf("  ... y más portales\n");  
    }  
      
    // Contar totales  
    int portals = 0, solids = 0;  
    for (int i = 0; i < map->num_walls; i++) {  
        if (map->walls[i]) {  
            if (map->walls[i]->front_region >= 0 && map->walls[i]->back_region >= 0) {  
                portals++;  
            } else {  
                solids++;  
            }  
        }  
    }  
      
    printf("DEBUG: Total - %d portales, %d paredes sólidas\n", portals, solids);  
}

void wld_build_sectors(WLD_Map *map)  
{  
    printf("DEBUG: Calculando back_region para todas las paredes...\n");  
      
    // 1. Normalizar orientación de paredes  
    for (int i = 0; i < map->num_walls; i++) {  
        WLD_Wall *wall = map->walls[i];  
        if (!wall) continue;  
          
        if (wall->p1 > wall->p2) {  
            int aux = wall->p1;  
            wall->p1 = wall->p2;  
            wall->p2 = aux;  
        }  
        wall->back_region = -1;  
        wall->texture_top = 0;  
        wall->texture_bot = 0;  
    }  
      
    // 2. Buscar portales por vértices compartidos  
    for (int i = 0; i < map->num_walls; i++) {  
        WLD_Wall *wall1 = map->walls[i];  
        if (!wall1) continue;  
          
        for (int j = i+1; j < map->num_walls; j++) {  
            WLD_Wall *wall2 = map->walls[j];  
            if (!wall2) continue;  
              
            // MISMA PARED, ORIENTACIÓN OPUESTA = PORTAL  
            if (wall1->p1 == wall2->p2 && wall1->p2 == wall2->p1) {  
                if (wall1->front_region != wall2->front_region) {  
                    wall1->back_region = wall2->front_region;  
                    wall2->back_region = wall1->front_region;  
                    wall1->type = 1; // Portal  
                    wall2->type = 1; // Portal  
                      
                    // Portal transparente en medio  
                    // Primero guardar la textura para las partes superior e inferior
                    wall1->texture_top = wall1->texture;
                    wall1->texture_bot = wall1->texture;
                    wall2->texture_top = wall2->texture;
                    wall2->texture_bot = wall2->texture;
                    
                    // Luego hacer transparente el centro
                    wall1->texture = 0;  
                    wall2->texture = 0;  

                }  
            }  
        }  
    }  

    // 2.5. Búsqueda GEOMÉTRICA (Fuzzy Match) para vértices duplicados
    // Esto arregla casos donde el mapa tiene vértices duplicados (misma posición, distinto ID)
    printf("DEBUG: Buscando portales por coincidencia geométrica (fuzzy)...\n");
    for (int i = 0; i < map->num_walls; i++) {
        WLD_Wall *wall1 = map->walls[i];
        if (!wall1 || wall1->back_region != -1) continue;

        for (int j = i + 1; j < map->num_walls; j++) {
            WLD_Wall *wall2 = map->walls[j];
            if (!wall2 || wall2->back_region != -1) continue;

            // Deben estar en regiones diferentes
            if (wall1->front_region == wall2->front_region) continue;

            // Coordenadas de los puntos
            float x1_a = map->points[wall1->p1]->x;
            float y1_a = map->points[wall1->p1]->y;
            float x2_a = map->points[wall1->p2]->x;
            float y2_a = map->points[wall1->p2]->y;

            float x1_b = map->points[wall2->p1]->x;
            float y1_b = map->points[wall2->p1]->y;
            float x2_b = map->points[wall2->p2]->x;
            float y2_b = map->points[wall2->p2]->y;

            // Verificar si coinciden invertidos (A.p1 ~ B.p2 Y A.p2 ~ B.p1)
            float dist1 = sqrtf(powf(x1_a - x2_b, 2) + powf(y1_a - y2_b, 2));
            float dist2 = sqrtf(powf(x2_a - x1_b, 2) + powf(y2_a - y1_b, 2));

            if (dist1 < 1.0f && dist2 < 1.0f) {
                // ¡COINCIDENCIA GEOMÉTRICA!
                wall1->back_region = wall2->front_region;
                wall2->back_region = wall1->front_region;
                wall1->type = 1;
                wall2->type = 1;
                
                // Asignar texturas (Smart Texture Assignment)
                // Para wall1
                wall1->texture_top = wall1->texture;
                wall1->texture_bot = wall1->texture;
                if (wall1->texture_top <= 0) {
                     // Buscar en vecinos de wall1
                     for (int k = 0; k < map->num_walls; k++) {
                        if (map->walls[k]->front_region == wall1->front_region && map->walls[k]->texture > 0) {
                            wall1->texture_top = map->walls[k]->texture;
                            wall1->texture_bot = map->walls[k]->texture;
                            break;
                        }
                    }
                    // Fallback techo/suelo
                    WLD_Region *front1 = map->regions[wall1->front_region];
                    if (wall1->texture_top <= 0 && front1) {
                        if (front1->ceil_tex > 0) wall1->texture_top = front1->ceil_tex;
                        if (front1->floor_tex > 0) wall1->texture_bot = front1->floor_tex;
                    }
                }
                if (wall1->texture_bot <= 0) wall1->texture_bot = wall1->texture_top;
                wall1->texture = 0;

                // Para wall2
                wall2->texture_top = wall2->texture;
                wall2->texture_bot = wall2->texture;
                if (wall2->texture_top <= 0) {
                     // Buscar en vecinos de wall2
                     for (int k = 0; k < map->num_walls; k++) {
                        if (map->walls[k]->front_region == wall2->front_region && map->walls[k]->texture > 0) {
                            wall2->texture_top = map->walls[k]->texture;
                            wall2->texture_bot = map->walls[k]->texture;
                            break;
                        }
                    }
                    // Fallback techo/suelo
                    WLD_Region *front2 = map->regions[wall2->front_region];
                    if (wall2->texture_top <= 0 && front2) {
                        if (front2->ceil_tex > 0) wall2->texture_top = front2->ceil_tex;
                        if (front2->floor_tex > 0) wall2->texture_bot = front2->floor_tex;
                    }
                }
                if (wall2->texture_bot <= 0) wall2->texture_bot = wall2->texture_top;
                wall2->texture = 0;
            }
        }
    }
      
    // 3. Para paredes sin back_region, buscar región adyacente  
    for (int i = 0; i < map->num_walls; i++) {  
        WLD_Wall *wall = map->walls[i];  
        if (wall->back_region == -1 && wall->front_region >= 0) {  
            // Calcular vector de la pared
            float dx = map->points[wall->p2]->x - map->points[wall->p1]->x;
            float dy = map->points[wall->p2]->y - map->points[wall->p1]->y;
            float len = sqrtf(dx*dx + dy*dy);
            
            int region_back = -1;
            
            // Punto medio (para fallback y cálculo)
            float mid_x = (map->points[wall->p1]->x + map->points[wall->p2]->x) / 2.0f;
            float mid_y = (map->points[wall->p1]->y + map->points[wall->p2]->y) / 2.0f;

            if (len > 0.001f) {
                // Normalizar y rotar 90 grados para obtener la normal
                float nx = -dy / len;
                float ny = dx / len;
                
                // Puntos a probar: 10%, 50%, 90%
                float offsets_t[] = {0.5f, 0.1f, 0.9f};
                // Distancias moderadas (2 y 8 unidades)
                float dist_offsets[] = {2.0f, 8.0f};
                
                bool found = false;
                
                for (int d = 0; d < 2; d++) {
                    float dist_offset = dist_offsets[d];
                    
                    for (int k = 0; k < 3; k++) {
                        float t = offsets_t[k];
                        float px = map->points[wall->p1]->x + dx * t;
                        float py = map->points[wall->p1]->y + dy * t;
                        
                        // Prueba 1: Lado A (Normal positiva)
                        int r = wld_find_region(map, px + nx * dist_offset, py + ny * dist_offset, wall->front_region);
                        if (r != -1) {
                            region_back = r;
                            found = true;
                            break;
                        }
                        
                        // Prueba 2: Lado B (Normal negativa)
                        r = wld_find_region(map, px - nx * dist_offset, py - ny * dist_offset, wall->front_region);
                        if (r != -1) {
                            region_back = r;
                            found = true;
                            break;
                        }
                    }
                    if (found) break;
                }
            }
            
            // FALLBACK: Si el método robusto falló, intentar el método original (punto medio exacto)
            if (region_back == -1) {
                region_back = wld_find_region(map, mid_x, mid_y, wall->front_region);
            }

            if (region_back != -1) {
                // SIEMPRE crear el portal, incluso si está "cerrado" verticalmente.
                // El motor de renderizado se encargará de hacer clipping correcto.
                // Esto evita que aparezcan paredes azules (cielo) cuando hay un portal cerrado.
                
                wall->back_region = region_back;  
                wall->type = 1; // Portal  
                
                // Asignar texturas para geometría compleja
                wall->texture_top = wall->texture;
                wall->texture_bot = wall->texture;
                
                // BÚSQUEDA INTELIGENTE DE TEXTURA
                // Si la pared no tenía textura (era invisible/azul), buscar una de un vecino
                if (wall->texture_top <= 0) {
                    WLD_Region *front = map->regions[wall->front_region];
                    
                    // 1. Intentar buscar otra pared en la misma región que tenga textura
                    for (int k = 0; k < map->num_walls; k++) {
                        if (map->walls[k]->front_region == wall->front_region && map->walls[k]->texture > 0) {
                            wall->texture_top = map->walls[k]->texture;
                            wall->texture_bot = map->walls[k]->texture;
                            break;
                        }
                    }
                    
                    // 2. Si falló, usar texturas de suelo/techo si están disponibles
                    if (wall->texture_top <= 0 && front) {
                        if (front->ceil_tex > 0) wall->texture_top = front->ceil_tex;
                        if (front->floor_tex > 0) wall->texture_bot = front->floor_tex;
                    }
                }
                
                // Asegurar que bot tenga algo si top tiene algo
                if (wall->texture_bot <= 0) wall->texture_bot = wall->texture_top;
                
                wall->texture = 0; // Transparente (el agujero del portal)
            }
            

        }  
    }  
    
    printf("DEBUG: Portales detectados y configurados como transparentes\n");  
}

void debug_complex_wall_detection(WLD_Map *map, int region_idx, int adjacent_idx)  
{  
    //printf("\n=== DEBUG DETECCIÓN GEOMETRÍA COMPLEJA ===\n");  
      
    // Validar índices de región  
    if (region_idx < 0 || region_idx >= map->num_regions ||  
        adjacent_idx < 0 || adjacent_idx >= map->num_regions) {  
       // printf("ERROR: Índices inválidos - region_idx=%d (max=%d), adjacent_idx=%d (max=%d)\n",  
              // region_idx, map->num_regions-1, adjacent_idx, map->num_regions-1);  
        return;  
    }  
      
    // Obtener regiones  
    WLD_Region *current = map->regions[region_idx];  
    WLD_Region *adjacent = map->regions[adjacent_idx];  
      
    // Validar punteros  
    if (!current) {  
       // printf("ERROR: Región actual[%d] es NULL\n", region_idx);  
        return;  
    }  
    if (!adjacent) {  
     //   printf("ERROR: Región adyacente[%d] es NULL\n", adjacent_idx);  
        return;  
    }  
      
    // Información básica de regiones  
    // printf("Región actual[%d]:\n", region_idx);  
    // printf("  floor_height: %d\n", current->floor_height);  
    // printf("  ceil_height: %d\n", current->ceil_height);  
    // printf("  active: %s\n", current->active ? "SÍ" : "NO");  
    // printf("  floor_tex: %d\n", current->floor_tex);  
    // printf("  ceil_tex: %d\n", current->ceil_tex);  
      
    // printf("Región adyacente[%d]:\n", adjacent_idx);  
    // printf("  floor_height: %d\n", adjacent->floor_height);  
    // printf("  ceil_height: %d\n", adjacent->ceil_height);  
    // printf("  active: %s\n", adjacent->active ? "SÍ" : "NO");  
    // printf("  floor_tex: %d\n", adjacent->floor_tex);  
    // printf("  ceil_tex: %d\n", adjacent->ceil_tex);  
      
    // Calcular diferencias  
    int floor_diff = current->floor_height - adjacent->floor_height;  
    int ceil_diff = current->ceil_height - adjacent->ceil_height;  
      
   // printf("\nAnálisis de diferencias:\n");  
  //  printf("  Diferencia floor: %d (%s)\n", floor_diff,   
   
//     printf("  Diferencia ceil: %d (%s)\n", ceil_diff,   
        //   ceil_diff != 0 ? "COMPLEJO" : "igual");  
      
    // Determinar si debería ser complejo  
    bool should_be_complex = (floor_diff != 0) || (ceil_diff != 0);  
   // printf("\nResultado: %s\n", should_be_complex ?   
           //"DEBERÍA SER GEOMETRÍA COMPLEJA" : "geometría simple");  
      
    // Buscar paredes que conectan estas regiones  
  // printf("\nParedes que conectan región[%d] con región[%d]:\n", region_idx, adjacent_idx);  
    int connecting_walls = 0;  
      
    for (int i = 0; i < map->num_walls; i++) {  
        WLD_Wall *wall = map->walls[i];  
        if (!wall) continue;  
          
        bool connects = false;  
        if ((wall->front_region == region_idx && wall->back_region == adjacent_idx) ||  
            (wall->front_region == adjacent_idx && wall->back_region == region_idx)) {  
            connects = true;  
        }  
          
        if (connects) {  
            connecting_walls++;  
            printf("  Pared[%d]: p1=%d, p2=%d, type=%d\n",   
                   i, wall->p1, wall->p2, wall->type);  
            printf("    texture: %d, texture_top: %d, texture_bot: %d\n",  
                   wall->texture, wall->texture_top, wall->texture_bot);  
              
            // Validar texturas para portales  
            if (wall->type == 1) {  
                if (wall->texture_top == 0 || wall->texture_bot == 0) {  
                    printf("    ⚠️  ADVERTENCIA: Portal sin texturas top/bot\n");  
                }  
            }  
        }  
    }  
      
    if (connecting_walls == 0) {  
        printf("  ❌ No se encontraron paredes que conecten estas regiones\n");  
    } else {  
        printf("  ✓ Encontradas %d paredes conectando las regiones\n", connecting_walls);  
    }  
      
    printf("=== FIN DEBUG ===\n\n");  
}


// Calcular regiones anidadas después de asignar back_region
void wld_calculate_nested_regions(WLD_Map *map)
{
    // DEBUG: Mostrar types de regiones
    printf("DEBUG: Types de regiones (primeras 21):\n");
    for (int i = 0; i < map->num_regions && i < 21; i++) {
        if (map->regions[i]) {
            printf("  Región %d: type=%d\n", i, map->regions[i]->type);
        }
    }
    
    // Pre-calcular regiones anidadas basándose en type
    // Regiones con type más alto están más anidadas
    int processed_pairs[256][256] = {0};
    
    for (int i = 0; i < map->num_walls; i++) {
        WLD_Wall *wall = map->walls[i];
        if (!wall) continue;
        
        int front = wall->front_region;
        int back = wall->back_region;
        
        if (back < 0 || back >= map->num_regions) continue;
        if (front < 0 || front >= map->num_regions) continue;
        if (front == back) continue;
        
        // Evitar procesar el mismo par dos veces
        if (front < 256 && back < 256) {
            if (processed_pairs[front][back] || processed_pairs[back][front]) continue;
            processed_pairs[front][back] = 1;
        }
        
        // Determinar cuál región es padre basándose en TAMAÑO
        // La región con MÁS paredes es el padre (más grande)
        int parent_idx, child_idx;
        int front_walls = optimized_regions[front].num_wall_ptrs;
        int back_walls = optimized_regions[back].num_wall_ptrs;
        
        if (front_walls > back_walls) {
            // front tiene más paredes, es el padre
            parent_idx = front;
            child_idx = back;
        } else if (back_walls > front_walls) {
            // back tiene más paredes, es el padre
            parent_idx = back;
            child_idx = front;
        } else {
            // Mismo número de paredes, usar back_region como criterio
            parent_idx = back;
            child_idx = front;
        }
        
        WLD_Region_Optimized *parent = &optimized_regions[parent_idx];
        
        int already_added = 0;
        for (int j = 0; j < parent->num_nested_regions; j++) {
            if (parent->nested_regions[j] == child_idx) {
                already_added = 1;
                break;
            }
        }
        
        if (!already_added && parent->num_nested_regions < 64) {
            parent->nested_regions[parent->num_nested_regions++] = child_idx;
        }
    }
    
    // DEBUG: Mostrar primeras relaciones
    printf("DEBUG: Primeras relaciones anidadas (primeras 50 paredes):\n");
    for (int i = 0; i < map->num_walls && i < 50; i++) {
        WLD_Wall *wall = map->walls[i];
        if (!wall) continue;
        if (wall->back_region >= 0 && wall->front_region >= 0) {
            printf("  Pared %d: front=%d, back=%d -> %d anidada en %d\n", 
                   i, wall->front_region, wall->back_region, 
                   wall->front_region, wall->back_region);
        }
    }
    
    // DEBUG
    printf("DEBUG: Regiones anidadas:\n");
    for (int i = 0; i < map->num_regions && i < 21; i++) {
        if (optimized_regions[i].num_nested_regions > 0) {
            printf("  Región %d: ", i);
            for (int j = 0; j < optimized_regions[i].num_nested_regions; j++) {
                printf("%d ", optimized_regions[i].nested_regions[j]);
            }
            printf("\n");
        }
    }
}

int64_t libmod_wld_set_wld_fov(INSTANCE *my, int64_t *params) {  
    float new_fov = *(float*)&params[0];  
      
    if (new_fov < 0.001f || new_fov > 0.01f) {  
        fprintf(stderr, "Error: wld_fov debe estar entre 0.001 y 0.01\n");  
        return 0;  
    }  
      
    wld_angle_step = new_fov;  
    wld_focal_length = 0.9f / wld_angle_step;
    return 1;  
}

void validate_and_fix_portals(WLD_Map *map) {  
    if (!map) return;  
      
    int fixed_count = 0;  
    for (int i = 0; i < map->num_walls; i++) {  
        WLD_Wall *wall = map->walls[i];  
        if (!wall) continue;  
          
        // Si es portal pero tiene textura en medio, corregir  
        if (wall->type == 1 && wall->texture > 0) {  
            printf("CORRECCIÓN: Portal[%d] tenía texture=%d, estableciendo a 0\n",   
                   i, wall->texture);  
            wall->texture = 0;  
            fixed_count++;  
        }  
          
        // Asegurar que portales tengan texturas top/bot  
        if (wall->type == 1 && wall->back_region >= 0) {  
            if (wall->texture_top == 0 && map->regions[wall->front_region]) {  
                wall->texture_top = map->regions[wall->front_region]->ceil_tex;  
            }  
            if (wall->texture_bot == 0 && map->regions[wall->front_region]) {  
                wall->texture_bot = map->regions[wall->front_region]->floor_tex;  
            }  
        }  
    }  
      
    if (fixed_count > 0) {  
        printf("DEBUG: Se corrigieron %d portales con textura incorrecta\n", fixed_count);  
    }  
}

void export_map_to_text(WLD_Map *map, const char *filename) {  
    FILE *f = fopen(filename, "w");  
    if (!f) return;  
      
    fprintf(f, "PUNTOS:\n");  
    for (int i = 0; i < map->num_points; i++) {  
        fprintf(f, "  %d: (%d,%d)\n", i,   
                map->points[i]->x, map->points[i]->y);  
    }  
      
    fprintf(f, "\nPAREDES:\n");  
    for (int i = 0; i < map->num_walls; i++) {  
        WLD_Wall *wall = map->walls[i];  
        fprintf(f, "  %d: p1=%d, p2=%d, front=%d, back=%d, type=%d\n",  
                i, wall->p1, wall->p2, wall->front_region,   
                wall->back_region, wall->type);  
    }  
      
    fclose(f);  
}

// Añade este código para analizar las coordenadas  
void analyze_wall_coordinates(WLD_Map *map) {  
    printf("DEBUG: Analizando coordenadas de paredes...\n");  
    for (int i = 0; i < 10 && i < map->num_walls; i++) {  
        WLD_Wall *wall = map->walls[i];  
        if (!wall) continue;  
          
        printf("Pared[%d]: p1=(%d,%d), p2=(%d,%d)\n",  
               i,   
               map->points[wall->p1]->x, map->points[wall->p1]->y,  
               map->points[wall->p2]->x, map->points[wall->p2]->y);  
    }  
}


void render_wall_section(WLD_Map *map, int texture_index, int col,   
                        int y_start, int y_end, float wall_u, float fog_factor,   
                        char *section_name, float orig_top, float orig_height)  
{  
    if (y_start >= y_end) return;  
      
    if (texture_index <= 0) {  
        // No dibujar nada si no hay textura (evitar colores de debug)
        return;  
    }  
      
    GRAPH *tex_graph = get_tex_image(texture_index);  
    if (!tex_graph) return;  
      
    int tex_x = (int)(wall_u * tex_graph->width) % tex_graph->width;  
    if (tex_x < 0) tex_x = 0;  
    if (tex_x >= tex_graph->width) tex_x = tex_graph->width - 1;  
      
    // float wall_height = y_end - y_start; // YA NO SE USA PARA MAPPING
      
    for (int y = y_start; y < y_end; y++) {  
        // Usar altura original proyectada para el mapeo de textura
        float v = (float)(y - orig_top) / orig_height;  
        int tex_y = (int)(v * tex_graph->height) % tex_graph->height;
        
        // Clampear tex_y por seguridad
        if (tex_y < 0) tex_y = 0;
        if (tex_y >= tex_graph->height) tex_y = tex_graph->height - 1;
          
        uint32_t pixel = gr_get_pixel(tex_graph, tex_x, tex_y);  
          
        uint8_t r = (pixel >> gPixelFormat->Rshift) & 0xFF;  
        uint8_t g = (pixel >> gPixelFormat->Gshift) & 0xFF;  
        uint8_t b = (pixel >> gPixelFormat->Bshift) & 0xFF;  
          
        r = (uint8_t)(r * fog_factor);  
        g = (uint8_t)(g * fog_factor);  
        b = (uint8_t)(b * fog_factor);  
          
        uint32_t color = SDL_MapRGB(gPixelFormat, r, g, b);  
        gr_put_pixel(render_buffer, col, y, color);  
    }  
}

void render_complex_wall_section(WLD_Map *map, WLD_Wall *wall, WLD_Region *region,      
                                int region_idx, int col, int screen_w, int screen_h,      
                                int y_start, int y_end, float wall_u, float fog_factor,      
                                float cam_x, float cam_y, float cam_z, float hit_distance,  
                                int clip_top, int clip_bottom)      
{      
    // Validar región adyacente      
    WLD_Region *adjacent_region = NULL;      
    int adjacent_idx = -1;      
          
    if (wall->front_region == region_idx) {      
        adjacent_idx = wall->back_region;      
    } else if (wall->back_region == region_idx) {      
        adjacent_idx = wall->front_region;      
    }      
          
    if (adjacent_idx < 0 || adjacent_idx >= map->num_regions) {      
        // printf("DEBUG: Región adyacente inválida %d\n", adjacent_idx);      
        render_wall_column(map, wall, region_idx, col, screen_w, screen_h,      
                          cam_x, cam_y, cam_z, hit_distance, clip_top, clip_bottom);      
        return;      
    }      
          
    adjacent_region = map->regions[adjacent_idx];      
    if (!adjacent_region || !adjacent_region->active) {      
        // printf("DEBUG: Región adyacente nula o inactiva %d\n", adjacent_idx);      
        render_wall_column(map, wall, region_idx, col, screen_w, screen_h,      
                          cam_x, cam_y, cam_z, hit_distance, clip_top, clip_bottom);      
        return;      
    }      
          
    // CORRECCIÓN FISHEYE: Usar distancia corregida
    float angle_offset = ((float)col - screen_w/2.0f) * wld_angle_step;
    float corrected_distance = hit_distance * cos(angle_offset);
    if (corrected_distance < 0.1f) corrected_distance = 0.1f;

    // Factor de proyección VPE      
    float t1 = wld_focal_length / corrected_distance;      
          
    // Calcular alturas proyectadas correctamente (Y=0 arriba)      
    float curr_floor_t = (cam_z - region->floor_height);      
    float curr_ceil_t = (cam_z - region->ceil_height);      
    float adj_floor_t = (cam_z - adjacent_region->floor_height);      
    float adj_ceil_t = (cam_z - adjacent_region->ceil_height);      
          
    // Usar fórmula consistente con render_wall_column      
    int cf_y = (screen_h/2.0f) + curr_floor_t * t1;  // Y positivo = abajo      
    int cc_y = (screen_h/2.0f) + curr_ceil_t * t1;   // Y positivo = abajo      
    int af_y = (screen_h/2.0f) + adj_floor_t * t1;      
    int ac_y = (screen_h/2.0f) + adj_ceil_t * t1;      
          
    // Asegurar orden correcto (techo arriba, piso abajo)      
    if (cf_y < cc_y) {      
        int temp = cf_y; cf_y = cc_y; cc_y = temp;      
    }      
    if (af_y < ac_y) {      
        int temp = af_y; af_y = ac_y; ac_y = temp;      
    }      
          
    // Limitar a pantalla      
    // cf_y = (cf_y < 0) ? 0 : (cf_y >= screen_h) ? screen_h-1 : cf_y;      
    // cc_y = (cc_y < 0) ? 0 : (cc_y >= screen_h) ? screen_h-1 : cc_y;      
    // af_y = (af_y < 0) ? 0 : (af_y >= screen_h) ? screen_h-1 : af_y;      
    // ac_y = (ac_y < 0) ? 0 : (ac_y >= screen_h) ? screen_h-1 : ac_y;      
          
    // CORRECCIÓN: Calcular wall_u si no se proporcionó      
    if (wall_u == 0.0f) {      
        int p1 = wall->p1;      
        int p2 = wall->p2;      
        if (p1 >= 0 && p1 < map->num_points && p2 >= 0 && p2 < map->num_points) {      
            float x1 = map->points[p1]->x;      
            float y1 = map->points[p1]->y;      
            float x2 = map->points[p2]->x;      
            float y2 = map->points[p2]->y;      
                  
            // Calcular punto de impacto del rayo      
            // angle_offset ya calculado
            float ray_dir_x = cos(camera.angle + angle_offset);      
            float ray_dir_y = sin(camera.angle + angle_offset);      
                  
            float hit_x = cam_x + ray_dir_x * hit_distance;      
            float hit_y = cam_y + ray_dir_y * hit_distance;      
                  
            // Calcular wall_u como distancia normalizada desde p1      
            float wall_dx = x2 - x1;      
            float wall_dy = y2 - y1;      
            float wall_len = sqrtf(wall_dx * wall_dx + wall_dy * wall_dy);      
                  
            if (wall_len > 0.001f) {      
                float t = ((hit_x - x1) * wall_dx + (hit_y - y1) * wall_dy) / (wall_len * wall_len);      
                wall_u = (t < 0.0f) ? 0.0f : (t > 1.0f) ? 1.0f : t;      
            }      
        }      
    }      
          
    // Renderizar sección superior (techo más bajo)      
    if (cc_y < ac_y) {  
        // Clampear con clips  
        int draw_start = (cc_y < clip_top) ? clip_top : cc_y;  
        int draw_end = (ac_y > clip_bottom) ? clip_bottom : ac_y;  
        
        float orig_height = (float)(ac_y - cc_y);
        if (orig_height < 1.0f) orig_height = 1.0f;
          
        if (draw_start < draw_end) {  
            render_wall_section(map, wall->texture_top, col, draw_start, draw_end,      
                           wall_u, fog_factor, "SUPERIOR", (float)cc_y, orig_height);      
        }  
    }      
          
    // Renderizar sección media (portal visible) - CORREGIDO  
    int mid_top = (cc_y > ac_y) ? cc_y : ac_y;      
    int mid_bot = (cf_y < af_y) ? cf_y : af_y;      
      
    // Clampear mid con clips  
    int draw_mid_top = (mid_top < clip_top) ? clip_top : mid_top;  
    int draw_mid_bot = (mid_bot > clip_bottom) ? clip_bottom : mid_bot;  
    
    float mid_height = (float)(mid_bot - mid_top);
    if (mid_height < 1.0f) mid_height = 1.0f;
      
    if (draw_mid_top < draw_mid_bot) {  
        if (wall->texture > 0) {  
            // Renderizar textura del portal si existe  
            render_wall_section(map, wall->texture, col, draw_mid_top, draw_mid_bot,    
                           wall_u, fog_factor, "MEDIO", (float)mid_top, mid_height);    
        }  
        // Si wall->texture == 0, NO renderizar nada aquí  
        // El raycasting continuará y mostrará el sector adyacente  
    }      
          
    // Renderizar sección inferior (piso más alto)      
    if (af_y < cf_y) {      
        // Clampear con clips  
        int draw_start = (af_y < clip_top) ? clip_top : af_y;  
        int draw_end = (cf_y > clip_bottom) ? clip_bottom : cf_y;  
        
        float orig_height = (float)(cf_y - af_y);
        if (orig_height < 1.0f) orig_height = 1.0f;
          
        if (draw_start < draw_end) {  
            render_wall_section(map, wall->texture_bot, col, draw_start, draw_end,      
                           wall_u, fog_factor, "INFERIOR", (float)af_y, orig_height);      
        }  
    }      
          
    // Renderizar piso y techo de la región ACTUAL (hasta el portal)  
    // Usamos cc_y (techo proyectado) y cf_y (suelo proyectado) como límites  
    render_floor_and_ceiling(map, region, col, screen_w, screen_h,      
                            cc_y, cf_y, cam_x, cam_y, cam_z,      
                            hit_distance, fog_factor, clip_top, clip_bottom, angle_offset);      
}


#include "libmod_wld_exports.h"