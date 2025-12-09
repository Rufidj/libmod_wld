import "libmod_gfx";    
import "libmod_input";    
import "libmod_misc";    
import "libmod_wld";    
    
GLOBAL    
    int graph_id;    
    string assets_path;    
    int wld_map;    
    int current_fov_mode = 0;  // 0=normal, 1=ancho, 2=muy ancho  
END    
    
PROCESS main()    
PRIVATE    
    int cam_x, cam_y, cam_z, cam_angle, cam_pitch;    
    int fpg_id;    
    string wld_file = "test.wld";    
BEGIN    
    set_mode(640, 480);    
    set_fps(0, 0);    
    window_set_title("WLD 3D Render - FOV Test");    
    
    // Cargar FPG usando fpg_load de BennuGD2    
    fpg_id = fpg_load("textures.fpg");  
    
    // Cargar WLD - función correcta es LOAD_WLD    
    if (!LOAD_WLD(wld_file, fpg_id))    
        say("No se ha cargado el mapa: " + wld_file);    
        return;    
        say("Se ha cargado el mapa: " + wld_file);    
    end    
    
    // Configurar cámara inicial  
   //WLD_SET_CAMERA(1072, 7800, 1024, 0, 0, 60000); // camara original    
   // WLD_SET_CAMERA(5300, 5300, 1024, 0, 0, 60000);     
    WLD_SET_CAMERA(4500, 4200, 600, 0, 0, 60000); // subterraneo    
    //WLD_SET_CAMERA(5300, 2400, 1024, 0, 0, 60000); // lava 
    //WLD_SET_CAMERA(15300, 13400, 300, 0, 0, 60000); //test2 
    wld_display();    
    
    LOOP    
        // Controles de cámara    
        if (key(_w)) WLD_MOVE_FORWARD(3); end    
        if (key(_s)) WLD_MOVE_BACKWARD(3); end    
        if (key(_a)) WLD_STRAFE_LEFT(3); end    
        if (key(_d)) WLD_STRAFE_RIGHT(3); end    
    
        if (key(_left)) WLD_LOOK_HORIZONTAL(-1); end    
        if (key(_right)) WLD_LOOK_HORIZONTAL(1); end    
        if (key(_up)) WLD_LOOK_VERTICAL(1); end    
        if (key(_down)) WLD_LOOK_VERTICAL(-1); end    
    
        if (key(_q)) WLD_ADJUST_HEIGHT(-1); end    
        if (key(_e)) WLD_ADJUST_HEIGHT(1); end    
    
        // NUEVO: Cambiar FOV con teclas 1, 2, 3  
        if (key(_1))    
            current_fov_mode = 0;  // FOV normal (~55°)  
            say("FOV: Normal (55°)");  
        end  
        if (key(_2))    
            current_fov_mode = 1;  // FOV ancho (~110°)  
            say("FOV: Ancho (110°)");  
        end  
        if (key(_3))    
            current_fov_mode = 2;  // FOV muy ancho (~165°)  
            say("FOV: Muy Ancho (165°)");  
        end  
    
        // Obtener posición actual - solo 5 parámetros    
        //HEIGHTMAP_GET_CAMERA_POSITION(&cam_x, &cam_y, &cam_z, &cam_angle, &cam_pitch);    
        write(0, 10, 10, 0, "POS: " + cam_x + "," + cam_y + "," + cam_z);    
        write(0, 10, 30, 0, "WASD: Mover | QE: Subir/Bajar | Flechas: Rotar");    
        write(0, 10, 50, 0, "1/2/3: FOV Normal/Ancho/Muy Ancho | ESC: Salir");    
        write(0, 10, 70, 0, "FPS: " + frame_info.fps);    
    
        if (key(_esc)) exit(); end    
        FRAME;    
        WRITE_DELETE(all_text);    
    END    
END    
    
PROCESS wld_display()  
BEGIN  
    LOOP  
        switch(current_fov_mode)  
            case 0:  
                graph_id = HEIGHTMAP_RENDER_WLD_3D(320, 240, 0.003);  
            end;  
            case 1:  
                graph_id = HEIGHTMAP_RENDER_WLD_3D(640, 480, 0.003);  
            end;  
            case 2:  
                graph_id = HEIGHTMAP_RENDER_WLD_3D(960, 720, 0.003);  
            end;  
        end  
          
        if (graph_id)  
            graph = graph_id;  
            x = 320;  
            y = 240;  
            size = 200;  
        else  
            say("ERROR: No se pudo renderizar el WLD");  
        end  
  
        FRAME;  
    END  
END