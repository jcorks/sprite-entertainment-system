#include "../matte/src/matte_vm.h"
#include "../matte/src/matte.h"
#include "../matte/src/matte_array.h"
#include "../matte/src/matte_string.h"
#include <stdio.h>
#include "../native.h"
#include <SDL2/SDL.h>

#define LAYER_MIN -63
#define LAYER_MID 0
#define LAYER_MAX 64
#define SPRITE_MAX 4096
#define OSCILLATOR_MAX 1024
#define SPRITE_COUNT_TOTAL 65536
#define TILE_ID_MAX 0x8ffff


typedef enum {
    SES_DEVICE__KEYBOARD,
    
    SES_DEVICE__POINTER0,
    SES_DEVICE__POINTER1,
    SES_DEVICE__POINTER2,
    SES_DEVICE__POINTER3,

    SES_DEVICE__GAMEPAD0,
    SES_DEVICE__GAMEPAD1,
    SES_DEVICE__GAMEPAD2,
    SES_DEVICE__GAMEPAD3,

    
} SES_DeviceType;


typedef struct SES_Sprite SES_Sprite;
struct SES_Sprite{
    float x;
    float y;
    float rotation;
    float scaleX;
    float scaleY;
    float centerX;
    float centerY;
    int layer;
    int effect;
    int enabled;

    uint32_t palette;
    uint32_t tile;
    
     // previous active sprite in linked list
    SES_Sprite * prev;    
    
    // next active sprite in linked list
    SES_Sprite * next;
   
};



typedef struct {
    float x;
    float y;
    int layer;
    int effect;
    int enabled;
    int id;

    uint32_t palette;
} SES_Background;




typedef struct {
    sesVector_t back;
    sesVector_t midBack;
    sesVector_t midFront;
    sesVector_t front; 

} SES_Palette;

typedef struct {
    // SES_Sprite *
    matteArray_t * sprites;
    matteArray_t * bgs;

} SES_GraphicsLayer;


typedef struct {
    // matteValue_t of functions to call
    matteArray_t * callbacks;

    // uint32_t IDs waiting to be used.
    matteArray_t * dead;
} SES_InputCallbackSet;







typedef struct SES_ActiveSprite SES_ActiveSprite;

/*
typedef struct {

    // array of user callbacks for input.
    SES_InputCallbackSet inputs[SES_DEVICE__GAMEPAD3+1];

    // an array of SES_Sprite, representing all accessed 
    // sprites.
    SES_Sprite * sprites;   

    uint32_t activeSpriteCount;
    
    // linked list of active sprites
    // This ref is the head (prev == NULL)
    SES_Sprite * activeSprites;

    // an array of SES_Background, representing all accessed 
    // backgrounds.
    matteArray_t * bgs;
    
    SES_OscillatorContext osc;

    // all layers, drawn in order from 0, 63.
    SES_GraphicsLayer layers[128];
    
    
    // an array of SES_Palette, representing all accessed 
    // palettes
    matteArray_t * palettes;
    
    // user function called every frame    
    matteValue_t updateFunc;



} SES_Context;
*/





typedef struct {
    // SDL window
    SDL_Window    * window;
    

    
    matteVM_t * vm;





    // every cartridge has a 
    sesCartridge_t * mainCart;
    
    // delay before updating the next frame
    uint32_t frameUpdateDelayMS;
    
    // callback ID for the timer event emitter
    SDL_TimerID frameUpdateID;
    
    // whether the main and aux are swapped. Usually for debugging context
    int swapped;


    // current pointer
    int hasPointerMotionEvent;
    int hasPointerScrollEvent;


    int pointerX;
    int pointerY;
    int pointerScrollX;
    int pointerScrollY;    

} SES_SDL;

static SES_SDL sdl = {};



static matteValue_t ses_api_get_calling_bank(matteVM_t * vm, matteValue_t fn, const matteValue_t * args, void * userData);


static matteValue_t ses_api_sprite_attrib(matteVM_t * vm, matteValue_t fn, const matteValue_t * args, void * userData);
static matteValue_t ses_api_engine_attrib(matteVM_t * vm, matteValue_t fn, const matteValue_t * args, void * userData);
static matteValue_t ses_api_palette_attrib(matteVM_t * vm, matteValue_t fn, const matteValue_t * args, void * userData);
static matteValue_t ses_api_tile_attrib(matteVM_t * vm, matteValue_t fn, const matteValue_t * args, void * userData);
static matteValue_t ses_api_input_attrib(matteVM_t * vm, matteValue_t fn, const matteValue_t * args, void * userData);
static matteValue_t ses_api_audio_attrib(matteVM_t * vm, matteValue_t fn, const matteValue_t * args, void * userData);
static matteValue_t ses_api_bg_attrib(matteVM_t * vm, matteValue_t fn, const matteValue_t * args, void * userData);

static matteValue_t ses_api_palette_query(matteVM_t * vm, matteValue_t fn, const matteValue_t * args, void * userData);
static matteValue_t ses_api_tile_query(matteVM_t * vm, matteValue_t fn, const matteValue_t * args, void * userData);








static void ses_api_render() {
    // re-sort sprites and bgs into layer buckets;
    ses_cartridge_push_graphics(ses.mainCart, ses.graphics);
    ses_graphics_context_render(ses.graphics);
}






typedef enum {
    SESNSA_ENABLE,
    SESNSA_ROTATION,
    SESNSA_SCALEX,
    SESNSA_SCALEY,
    SESNSA_POSITIONX,
    SESNSA_POSITIONY,
    SESNSA_CENTERX,
    SESNSA_CENTERY,
    SESNSA_LAYER,
    SESNSA_TILEINDEX,
    SESNSA_EFFECT,
    SESNSA_PALETTE,
} SESNative_SpriteAttribs;




matteValue_t ses_sdl_sprite_attrib(matteVM_t * vm, matteValue_t fn, const matteValue_t * args, void * userData) {
    matteHeap_t * heap = matte_vm_get_heap(vm);

    uint32_t id = matte_value_as_number(heap, args[0]);
    if (id >= SPRITE_COUNT_TOTAL) {
        matte_vm_raise_error_string(vm, MATTE_VM_STR_CAST(vm, "Sprite ID referenced is not [0, 65534] and is out-of-bounds."));                
        return matte_heap_new_value(heap);    
    }

    


    uint32_t len = matte_value_object_get_number_key_count(heap, args[1]);
    uint32_t i;
    for(i = 0; i < len; i+=2) {
        matteValue_t * flag  = matte_value_object_array_at_unsafe(heap, args[1], i);
        matteValue_t * value = matte_value_object_array_at_unsafe(heap, args[1], i+1);

        SES_Sprite * spr = &sdl.main.sprites[id];
        switch((int)matte_value_as_number(heap, *flag)) {
          case SESNSA_ENABLE: {
            ses_cartridge_enable_sprite(cart, id, matte_value_as_number(heap, *value))
            break;
          }  
          case SESNSA_ROTATION:
            spr->rotation = matte_value_as_number(heap, *value);
            break;

          case SESNSA_SCALEX:
            spr->scaleX = matte_value_as_number(heap, *value);
            break;

          case SESNSA_SCALEY:
            spr->scaleY = matte_value_as_number(heap, *value);
            break;

          case SESNSA_POSITIONX:
            spr->x = matte_value_as_number(heap, *value);
            break;
          
          case SESNSA_POSITIONY:
            spr->y = matte_value_as_number(heap, *value);
            break;

          case SESNSA_CENTERX:
            spr->centerX = matte_value_as_number(heap, *value);
            break;

          case SESNSA_CENTERY:
            spr->centerY = matte_value_as_number(heap, *value);
            break;

          case SESNSA_LAYER:
            spr->layer = matte_value_as_number(heap, *value);
            if (spr->layer > LAYER_MAX) spr->layer = LAYER_MAX;
            if (spr->layer < LAYER_MIN) spr->layer = LAYER_MIN;
            break;

          case SESNSA_TILEINDEX:
            spr->tile = matte_value_as_number(heap, *value);
            break;
          
          case SESNSA_EFFECT:
            spr->effect = matte_value_as_number(heap, *value);
            break;

          case SESNSA_PALETTE:
            spr->palette = matte_value_as_number(heap, *value);
            break;
        }

    }
    return matte_heap_new_value(heap);
}





typedef enum {
    SESNOA_ENABLE,
    SESNOA_PERIODMS,
    SESNOA_ONCYCLE,
    SESNOA_GET
} SESNative_OscillatorAttribs;


static int osc_get_index(SES_Oscillator * p) {
    return (p - &sdl.main.osc.all[0]);
}

matteValue_t ses_sdl_oscillator_attrib(matteVM_t * vm, matteValue_t fn, const matteValue_t * args, void * userData) {
    matteHeap_t * heap = matte_vm_get_heap(vm);

    uint32_t id = matte_value_as_number(heap, args[0]);

    if (id >= OSCILLATOR_MAX) {
        matte_vm_raise_error_string(vm, MATTE_VM_STR_CAST(vm, "Oscillator accessed beyond limit"));
        return matte_heap_new_value(heap);
    }

    SES_Oscillator * osc = &sdl.main.osc.all[id];

    uint32_t len = matte_value_object_get_number_key_count(heap, args[1]);
    uint32_t i;
    int n;
    for(i = 0; i < len; i+=2) {
        matteValue_t * flag  = matte_value_object_array_at_unsafe(heap, args[1], i);
        matteValue_t * value = matte_value_object_array_at_unsafe(heap, args[1], i+1);

        switch((int)matte_value_as_number(heap, *flag)) {
          case SESNOA_ENABLE:
            ses_cartridge_enable_oscillator(ses.mainCart, id, matte_value_as_boolean(heap, *value));
            break;
            
          case SESNOA_PERIODMS:
            osc->lengthMS = matte_value_as_number(heap, *value);
            osc->endMS = osc->startMS + osc->lengthMS;
            break;
            
          case SESNOA_ONCYCLE:
            if (osc->function.value.id == value->value.id) break;
            if (osc->function.binID) {
                matte_value_object_pop_lock(heap, osc->function);
            }
            osc->function = *value;
            matte_value_object_push_lock(heap, osc->function);
            break;            
            
            
          case SESNOA_GET: {
            uint32_t prog = (osc->endMS - SDL_GetTicks()) / (double)osc->lengthMS;
            if (prog < 0) prog = 0;
            if (prog > 1) prog = 1;
            double frac = 0.5*(1+sin((prog) * (2*M_PI)));
            matteValue_t fracVal = matte_heap_new_value(heap);
            matte_value_into_number(heap, &fracVal, frac);
            return fracVal;
          }
    
        }

    }
    return matte_heap_new_value(heap);
}





typedef enum {
    SESNEA_UPDATERATE,
    SESNEA_UPDATEFUNC,
    SESNEA_RESOLUTION,
    SESNEA_CLIPBOARDGET,
    SESNEA_CLIPBOARDSET 
} SESNative_EngineAttribs_t;



matteValue_t ses_sdl_engine_attrib(matteVM_t * vm, matteValue_t fn, const matteValue_t * args, void * userData) {
    matteHeap_t * heap = matte_vm_get_heap(vm);
    printf("ENGINE   ID: %d\n",
        (int)matte_value_as_number(heap, args[0])
    );  
    
    switch((int)matte_value_as_number(heap, args[0])) {

      // function to call to update each frame, according to the user.
      case SESNEA_UPDATEFUNC:
        matte_value_object_pop_lock(heap, sdl.main.updateFunc);
        sdl.main.updateFunc = args[1];
        matte_value_object_push_lock(heap, sdl.main.updateFunc);
        break;
        
      case SESNEA_UPDATERATE:
        sdl.frameUpdateDelayMS = matte_value_as_number(heap, args[1]) * 1000;
        break;


      case SESNEA_CLIPBOARDGET: {
        char * clipboardRaw = SDL_GetClipboardText();
        matteValue_t strOut = matte_heap_new_value(heap);
        matteString_t * strVal = matte_string_create_from_c_str("%s", clipboardRaw ? clipboardRaw : "");
        matte_value_into_string(heap, &strOut, strVal);
        matte_string_destroy(strVal);
        SDL_free(clipboardRaw);

        return strOut;
      };

      case SESNEA_CLIPBOARDSET: {
        const matteString_t * str = matte_value_string_get_string_unsafe(heap, args[1]);
        SDL_SetClipboardText(matte_string_get_c_str(str));
      };



        
    }
    
    return matte_heap_new_value(heap);
}



typedef enum {
    SESNPA_BACK,
    SESNPA_MIDBACK,
    SESNPA_MIDFRONT,
    SESNPA_FRONT
} SESNative_PaletteAttribs_t;

matteValue_t ses_sdl_palette_attrib(matteVM_t * vm, matteValue_t fn, const matteValue_t * args, void * userData) {
    matteHeap_t * heap = matte_vm_get_heap(vm);

    uint32_t id = matte_value_as_number(heap, args[0]);
    
    while (id >= matte_array_get_size(sdl.main.palettes)) {
        SES_Palette p = {};
        matte_array_push(sdl.main.palettes, p);
    }
    SES_Palette * p = &matte_array_at(sdl.main.palettes, SES_Palette, id);



    switch((int)matte_value_as_number(heap, args[1])) {
      case SESNPA_BACK:
        p->back.x = matte_value_as_number(heap, args[2]);
        p->back.y = matte_value_as_number(heap, args[3]);
        p->back.z = matte_value_as_number(heap, args[4]);
        break;

      case SESNPA_MIDBACK:
        p->midBack.x = matte_value_as_number(heap, args[2]);
        p->midBack.y = matte_value_as_number(heap, args[3]);
        p->midBack.z = matte_value_as_number(heap, args[4]);
        break;

      case SESNPA_MIDFRONT:
        p->midFront.x = matte_value_as_number(heap, args[2]);
        p->midFront.y = matte_value_as_number(heap, args[3]);
        p->midFront.z = matte_value_as_number(heap, args[4]);
        break;

      case SESNPA_FRONT:
        p->front.x = matte_value_as_number(heap, args[2]);
        p->front.y = matte_value_as_number(heap, args[3]);
        p->front.z = matte_value_as_number(heap, args[4]);
        break;

            
    }
    return matte_heap_new_value(heap);
}

typedef enum {
    SESNTA_BIND,
    SESNTA_SETTEXEL,
    SESNTA_UNBIND,
    SESNTA_COPY
} SESNative_TileAttribs_t;


matteValue_t ses_sdl_tile_attrib(matteVM_t * vm, matteValue_t fn, const matteValue_t * args, void * userData) {
    matteHeap_t * heap = matte_vm_get_heap(vm);
    printf("TILE     ID: %d, ATTRIB: %d\n",
        (int)matte_value_as_number(heap, args[0]),
        (int)matte_value_as_number(heap, args[1])
    );  
    switch((int)matte_value_as_number(heap, args[1])) {
      case SESNTA_BIND:
        ses_sdl_gl_bind_tile(matte_value_as_number(heap, args[0]));
        break;
        
      case SESNTA_SETTEXEL: {
        int pixel = matte_value_as_number(heap, args[2]);
        if (pixel < 0 || pixel > 4) {
            matte_vm_raise_error_string(vm, MATTE_VM_STR_CAST(vm, "Texture value data is not in the range 0 to 4."));
            return matte_heap_new_value(heap);
        }
        
        ses_sdl_gl_set_tile_pixel(
            matte_value_as_number(heap, args[0]),
            pixel
        );
        break;
      }
      case SESNTA_UNBIND:
        ses_sdl_gl_unbind_tile();
        break;
        
      case SESNTA_COPY:
        ses_sdl_gl_copy_from(
            matte_value_as_number(heap, args[0])
        );
        break;


    }
    return matte_heap_new_value(heap);
}


typedef enum {
    SESNIA_ADD,
    SESNIA_REMOVE
} SESNative_InputAction_t;

matteValue_t ses_sdl_input_attrib(matteVM_t * vm, matteValue_t fn, const matteValue_t * args, void * userData) {
    matteHeap_t * heap = matte_vm_get_heap(vm);
    printf("INPUT    ID: %d, ATTRIB: %d\n",
        (int)matte_value_as_number(heap, args[0]),
        (int)matte_value_as_number(heap, args[1])
    );  

    switch((int)matte_value_as_number(heap, args[0])) {
      case SESNIA_ADD: {
        SES_InputCallbackSet * set = &sdl.main.inputs[(int)matte_value_as_number(heap, args[1])];

        matteValue_t out = matte_heap_new_value(heap);
        uint32_t id;
        if (matte_array_get_size(set->dead)) {
            id = matte_array_at(set->dead, uint32_t, matte_array_get_size(set->dead)-1);
            matte_array_set_size(set->dead, matte_array_get_size(set->dead)-1);
        } else {
            matte_array_push(set->callbacks, args[2]);
            id = matte_array_get_size(set->callbacks)-1;
        }
        matte_array_at(set->callbacks, matteValue_t, id) = args[2];
        matte_value_object_push_lock(heap, args[2]);
        matte_value_into_number(heap, &out, id);
        return out;
      }
      
      
      case SESNIA_REMOVE: {
        SES_InputCallbackSet * set = &sdl.main.inputs[(int)matte_value_as_number(heap, args[1])];
        uint32_t id = matte_value_as_number(heap, args[2]);      
        
        if (id > matte_array_get_size(set->callbacks) ||
            matte_array_at(set->callbacks, matteValue_t, id).binID == 0) {
            break;   
        }
        matte_array_at(set->callbacks, matteValue_t, id).binID = 0;
        matte_array_push(set->dead, id);
      }
            
    }



    return matte_heap_new_value(heap);
}
matteValue_t ses_sdl_audio_attrib(matteVM_t * vm, matteValue_t fn, const matteValue_t * args, void * userData) {
    matteHeap_t * heap = matte_vm_get_heap(vm);
    printf("AUDIO    ID: %d, ATTRIB: %d\n",
        (int)matte_value_as_number(heap, args[0]),
        (int)matte_value_as_number(heap, args[1])
    );  
    return matte_heap_new_value(heap);
}



typedef enum {
    SESNBA_ENABLE,
    SESNBA_POSITIONX,
    SESNBA_POSITIONY,
    SESNBA_LAYER,
    SESNBA_EFFECT,
    SESNBA_PALETTE

} SESNative_BackgroundAttribs_t;

matteValue_t ses_sdl_bg_attrib(matteVM_t * vm, matteValue_t fn, const matteValue_t * args, void * userData) {
    matteHeap_t * heap = matte_vm_get_heap(vm);
    /*
    printf("BG       ID: %d, ATTRIB: %d\n",
        (int)matte_value_as_number(heap, args[0]),
        (int)matte_value_as_number(heap, args[1])
    );
    */  

    uint32_t id = matte_value_as_number(heap, args[0]);
    while(id >= matte_array_get_size(sdl.main.bgs)) {
        SES_Background bg = {};
        bg.id = matte_array_get_size(sdl.main.bgs);
        matte_array_push(sdl.main.bgs, bg);
    }
    SES_Background * bg = &matte_array_at(sdl.main.bgs, SES_Background, id);
    switch((int)matte_value_as_number(heap, args[1])) {
      case SESNBA_ENABLE:
        bg->enabled = matte_value_as_number(heap, args[2]);
        break;

      case SESNBA_POSITIONX:
        bg->x = matte_value_as_number(heap, args[2]);
        break;
      
      case SESNBA_POSITIONY:
        bg->y = matte_value_as_number(heap, args[2]);
        break;

      case SESNBA_LAYER:
        bg->layer = matte_value_as_number(heap, args[2]);
        if (bg->layer > LAYER_MAX) bg->layer = LAYER_MAX;
        if (bg->layer < LAYER_MIN) bg->layer = LAYER_MIN;
        break;
      
      case SESNBA_EFFECT:
        bg->effect = matte_value_as_number(heap, args[2]);
        break;

      case SESNBA_PALETTE:
        bg->palette = matte_value_as_number(heap, args[2]);
        break;

            
    }
    return matte_heap_new_value(heap);
}




matteValue_t ses_sdl_palette_query(matteVM_t * vm, matteValue_t fn, const matteValue_t * args, void * userData) {
    matteHeap_t * heap = matte_vm_get_heap(vm);
    printf("P.QUERY  ID: %d, ATTRIB: %d\n",
        (int)matte_value_as_number(heap, args[0]),
        (int)matte_value_as_number(heap, args[1])
    );  
    
    
    uint32_t id = matte_value_as_number(heap, args[0]);
    if (id >= matte_array_get_size(sdl.main.palettes))
        return matte_heap_new_value(heap);

    SES_Palette * p = &matte_array_at(sdl.main.palettes, SES_Palette, id);


    sesVector_t * color;
    switch((int)matte_value_as_number(heap, args[1])) {
      case SESNPA_BACK:
        color = &p->back;
        break;

      case SESNPA_MIDBACK:
        color = &p->midBack;
        break;

      case SESNPA_MIDFRONT:
        color = &p->midFront;
        break;

      case SESNPA_FRONT:
        color = &p->front;
        break;

      default:    return matte_heap_new_value(heap);
    }
    
    matteValue_t out = matte_heap_new_value(heap);
    
    switch((int)matte_value_as_number(heap, args[2])) {
      case 0: matte_value_into_number(heap, &out, color->x); break;
      case 1: matte_value_into_number(heap, &out, color->y); break;
      case 2: matte_value_into_number(heap, &out, color->z); break;
    }
    
    return out;
    
    

}
matteValue_t ses_sdl_tile_query(matteVM_t * vm, matteValue_t fn, const matteValue_t * args, void * userData) {
    matteHeap_t * heap = matte_vm_get_heap(vm);
    matteValue_t out = matte_heap_new_value(heap);
    matte_value_into_number(
        heap,
        &out,
        ses_sdl_gl_get_tile_pixel(
            matte_value_as_number(heap, args[0])
        )
    );
    return out;
}




static SES_Context context_create() {
    SES_Context ctx = {};

    ctx.bgs = matte_array_create(sizeof(SES_Background));
    ctx.palettes = matte_array_create(sizeof(SES_Palette));
    ctx.sprites = calloc(1, sizeof(SES_Sprite) * SPRITE_COUNT_TOTAL);
    int i;
    for(i = 0; i < SPRITE_COUNT_TOTAL; ++i) {
        ctx.sprites[i].scaleX = 1;
        ctx.sprites[i].scaleY = 1;
    }

    for(i = 0; i <= SES_DEVICE__GAMEPAD3; ++i) {
        ctx.inputs[i].callbacks = matte_array_create(sizeof(matteValue_t));    
        ctx.inputs[i].dead = matte_array_create(sizeof(uint32_t));
    }
    

    
    for(i = 0; i < 128; ++i) {
        SES_GraphicsLayer * layer = &ctx.layers[i];
        layer->sprites = matte_array_create(sizeof(SES_Sprite *));
        layer->bgs = matte_array_create(sizeof(SES_Background *));
    }
    return ctx;
}

void ses_native_commit_rom(matte_t * m) {
    // nothing yet!
    if (SDL_Init(
        SDL_INIT_TIMER |
        SDL_INIT_VIDEO |
        SDL_INIT_AUDIO |
        SDL_INIT_GAMECONTROLLER            
    ) != 0) {
        printf("SDL2 subsystem init failure.\n");
        exit(1);
    }
    matteVM_t * vm = matte_get_vm(m);

    // all 3 modes require activating the core features.
    matte_vm_set_external_function_autoname(vm, MATTE_VM_STR_CAST(vm, "ses_native__get_calling_bank"), 0, ses_sdl_get_calling_bank, NULL);


    matte_vm_set_external_function_autoname(vm, MATTE_VM_STR_CAST(vm, "ses_native__sprite_attrib"), 2, ses_sdl_sprite_attrib, NULL);
    matte_vm_set_external_function_autoname(vm, MATTE_VM_STR_CAST(vm, "ses_native__engine_attrib"), 3, ses_sdl_engine_attrib, NULL);
    matte_vm_set_external_function_autoname(vm, MATTE_VM_STR_CAST(vm, "ses_native__palette_attrib"), 5, ses_sdl_palette_attrib, NULL);
    matte_vm_set_external_function_autoname(vm, MATTE_VM_STR_CAST(vm, "ses_native__tile_attrib"), 3, ses_sdl_tile_attrib, NULL);
    matte_vm_set_external_function_autoname(vm, MATTE_VM_STR_CAST(vm, "ses_native__input_attrib"), 3, ses_sdl_input_attrib, NULL);
    matte_vm_set_external_function_autoname(vm, MATTE_VM_STR_CAST(vm, "ses_native__audio_attrib"), 4, ses_sdl_audio_attrib, NULL);
    matte_vm_set_external_function_autoname(vm, MATTE_VM_STR_CAST(vm, "ses_native__bg_attrib"), 4, ses_sdl_bg_attrib, NULL);
    matte_vm_set_external_function_autoname(vm, MATTE_VM_STR_CAST(vm, "ses_native__oscillator_attrib"), 2, ses_sdl_oscillator_attrib, NULL);


    matte_vm_set_external_function_autoname(vm, MATTE_VM_STR_CAST(vm, "ses_native__palette_query"), 3, ses_sdl_palette_query, NULL);
    matte_vm_set_external_function_autoname(vm, MATTE_VM_STR_CAST(vm, "ses_native__tile_query"), 2, ses_sdl_tile_query, NULL);


    sdl.main = context_create();
    sdl.aux = context_create();    
    ses_sdl_gl_init(
        &sdl.window,
        &sdl.ctx
    );

    sdl.frameUpdateDelayMS = (1 / 60.0)*1000;
    
    SDL_AddTimer(sdl.frameUpdateDelayMS, ses_sdl_emit_frame_event, NULL);
   
}

void ses_native_swap_context() {
    SES_Context c = sdl.main;
    sdl.main = sdl.aux;
    sdl.aux = c;
    sdl.swapped = !sdl.swapped;
}


static void push_motion_callback() {
    matteHeap_t * heap = matte_vm_get_heap(sdl.vm);
    uint32_t i;
    uint32_t len = matte_array_get_size(sdl.main.inputs[SES_DEVICE__POINTER0].callbacks);
    if (len == 0) return;
   
    double xcon, ycon;
    int w, h;
    SDL_GetWindowSize(sdl.window, &w, &h);
    
    matteString_t * xStr = (matteString_t*)MATTE_VM_STR_CAST(sdl.vm, "x");
    matteString_t * yStr = (matteString_t*)MATTE_VM_STR_CAST(sdl.vm, "y");
    matteString_t * eventStr = (matteString_t*)MATTE_VM_STR_CAST(sdl.vm, "event");
    
    
    matteValue_t x = matte_heap_new_value(heap);
    matteValue_t y = matte_heap_new_value(heap);
    matteValue_t event = matte_heap_new_value(heap);
    
    matte_value_into_string(heap, &x, xStr);
    matte_value_into_string(heap, &y, yStr);
    matte_value_into_string(heap, &event, eventStr);
    
    
    matteValue_t xval = matte_heap_new_value(heap);
    matteValue_t yval = matte_heap_new_value(heap);
    matteValue_t eventVal = matte_heap_new_value(heap);                
    
    matte_value_into_number(heap, &xval, (sdl.pointerX / (float) w) * ses_sdl_gl_get_render_width());
    matte_value_into_number(heap, &yval, (sdl.pointerY / (float) h) * ses_sdl_gl_get_render_height());
    matte_value_into_number(heap, &eventVal, 0);



    matteValue_t namesArr[] = {event, x, y};
    matteValue_t valsArr[] = {eventVal, xval, yval};                
    
    for(i = 0; i < len; ++i) {
        matteValue_t val = matte_array_at(sdl.main.inputs[SES_DEVICE__POINTER0].callbacks, matteValue_t, i);    
        if (val.binID == 0) continue;

        // for safety
        matteArray_t names = MATTE_ARRAY_CAST(namesArr, matteValue_t, 3);
        matteArray_t vals = MATTE_ARRAY_CAST(valsArr, matteValue_t, 3);

        matte_vm_call(sdl.vm, val, &vals, &names, NULL);
        
    }
}


static void push_scroll_callback() {
    matteHeap_t * heap = matte_vm_get_heap(sdl.vm);
    uint32_t i;
    uint32_t len = matte_array_get_size(sdl.main.inputs[SES_DEVICE__POINTER0].callbacks);
    if (len == 0) return;

    matteString_t * xStr = (matteString_t*)MATTE_VM_STR_CAST(sdl.vm, "x");
    matteString_t * yStr = (matteString_t*)MATTE_VM_STR_CAST(sdl.vm, "y");
    matteString_t * eventStr = (matteString_t*)MATTE_VM_STR_CAST(sdl.vm, "event");
    
    
    matteValue_t x = matte_heap_new_value(heap);
    matteValue_t y = matte_heap_new_value(heap);
    matteValue_t event = matte_heap_new_value(heap);
    
    matte_value_into_string(heap, &x, xStr);
    matte_value_into_string(heap, &y, yStr);
    matte_value_into_string(heap, &event, eventStr);
    
    
    matteValue_t xval = matte_heap_new_value(heap);
    matteValue_t yval = matte_heap_new_value(heap);
    matteValue_t eventVal = matte_heap_new_value(heap);
    
    
    
    
    matte_value_into_number(heap, &xval, sdl.pointerScrollX);
    matte_value_into_number(heap, &yval, sdl.pointerScrollY);
    matte_value_into_number(heap, &eventVal, 5); // scroll



    matteValue_t namesArr[] = {event, x, y};
    matteValue_t valsArr[] = {eventVal, xval, yval};                
    
    for(i = 0; i < len; ++i) {
        matteValue_t val = matte_array_at(sdl.main.inputs[SES_DEVICE__POINTER0].callbacks, matteValue_t, i);    
        if (val.binID == 0) continue;

        // for safety
        matteArray_t names = MATTE_ARRAY_CAST(namesArr, matteValue_t, 3);
        matteArray_t vals = MATTE_ARRAY_CAST(valsArr, matteValue_t, 3);

        matte_vm_call(sdl.vm, val, &vals, &names, NULL);
        
    }

}

int ses_native_update(matte_t * m) {
    sdl.vm = matte_get_vm(m);
    matteHeap_t * heap = matte_vm_get_heap(sdl.vm);
    SDL_Event evt;

    int hadMotion = 0;
    while(SDL_PollEvent(&evt) != 0) {
        if (evt.type == SDL_QUIT) {
            // TODO: on quit callback via engine 
            exit(0);
            break;
        }            
        
        
        
        switch(evt.type) {
          case SDL_KEYUP: 
          case SDL_KEYDOWN: {          
          
            uint32_t i;
            uint32_t len = matte_array_get_size(sdl.main.inputs[SES_DEVICE__KEYBOARD].callbacks);
            if (len == 0) break;
            
            matteString_t * textStr = (matteString_t*)MATTE_VM_STR_CAST(sdl.vm, "key");
            matteString_t * eventStr = (matteString_t*)MATTE_VM_STR_CAST(sdl.vm, "event");
            
            
            matteValue_t text = matte_heap_new_value(heap);
            matteValue_t event = matte_heap_new_value(heap);
            
            matte_value_into_string(heap, &text, textStr);
            matte_value_into_string(heap, &event, eventStr);
            
            
            matteValue_t textval = matte_heap_new_value(heap);
            matteValue_t eventVal = matte_heap_new_value(heap);
            
            double xcon, ycon;
            int w, h;
            
            
            matte_value_into_number(heap, &textval, evt.key.keysym.sym);
            matte_value_into_number(heap, &eventVal, (evt.type == SDL_KEYDOWN ? 2 : 6));// key down



            matteValue_t namesArr[] = {event, text};
            matteValue_t valsArr[] = {eventVal, textval};                
            
            for(i = 0; i < len; ++i) {
                matteValue_t val = matte_array_at(sdl.main.inputs[SES_DEVICE__KEYBOARD].callbacks, matteValue_t, i);    
                if (val.binID == 0) continue;

                // for safety
                matteArray_t names = MATTE_ARRAY_CAST(namesArr, matteValue_t, 2);
                matteArray_t vals = MATTE_ARRAY_CAST(valsArr, matteValue_t, 2);

                matte_vm_call(sdl.vm, val, &vals, &names, NULL);
                
            }   
            break;                 
          } 
        
          case SDL_TEXTINPUT: {
          
          
            uint32_t i;
            uint32_t len = matte_array_get_size(sdl.main.inputs[SES_DEVICE__KEYBOARD].callbacks);
            if (len == 0) break;
            
            matteString_t * textStr = (matteString_t*)MATTE_VM_STR_CAST(sdl.vm, "text");
            matteString_t * eventStr = (matteString_t*)MATTE_VM_STR_CAST(sdl.vm, "event");
            
            
            matteValue_t text = matte_heap_new_value(heap);
            matteValue_t event = matte_heap_new_value(heap);
            
            matte_value_into_string(heap, &text, textStr);
            matte_value_into_string(heap, &event, eventStr);
            
            
            matteValue_t textval = matte_heap_new_value(heap);
            matteValue_t eventVal = matte_heap_new_value(heap);
            
            double xcon, ycon;
            int w, h;
            
            
            matteString_t * textRaw = matte_string_create_from_c_str("%s", evt.text.text);
            matte_value_into_string(heap, &textval, textRaw);
            matte_string_destroy(textRaw);
            matte_value_into_number(heap, &eventVal, 1);



            matteValue_t namesArr[] = {event, text};
            matteValue_t valsArr[] = {eventVal, textval};                
            
            for(i = 0; i < len; ++i) {
                matteValue_t val = matte_array_at(sdl.main.inputs[SES_DEVICE__KEYBOARD].callbacks, matteValue_t, i);    
                if (val.binID == 0) continue;

                // for safety
                matteArray_t names = MATTE_ARRAY_CAST(namesArr, matteValue_t, 2);
                matteArray_t vals = MATTE_ARRAY_CAST(valsArr, matteValue_t, 2);

                matte_vm_call(sdl.vm, val, &vals, &names, NULL);
                
            }   
            break;       
          }
        
        
          case SDL_MOUSEBUTTONDOWN:
          case SDL_MOUSEBUTTONUP: {
          
          
            uint32_t i;
            uint32_t len = matte_array_get_size(sdl.main.inputs[SES_DEVICE__POINTER0].callbacks);
            if (len == 0) break;
            
            matteString_t * xStr = (matteString_t*)MATTE_VM_STR_CAST(sdl.vm, "x");
            matteString_t * yStr = (matteString_t*)MATTE_VM_STR_CAST(sdl.vm, "y");
            matteString_t * buttonStr = (matteString_t*)MATTE_VM_STR_CAST(sdl.vm, "button");
            matteString_t * eventStr = (matteString_t*)MATTE_VM_STR_CAST(sdl.vm, "event");
            
            
            matteValue_t x = matte_heap_new_value(heap);
            matteValue_t y = matte_heap_new_value(heap);
            matteValue_t event = matte_heap_new_value(heap);
            matteValue_t button = matte_heap_new_value(heap);
            
            matte_value_into_string(heap, &x, xStr);
            matte_value_into_string(heap, &y, yStr);
            matte_value_into_string(heap, &button, buttonStr);
            matte_value_into_string(heap, &event, eventStr);
            
            
            matteValue_t xval = matte_heap_new_value(heap);
            matteValue_t yval = matte_heap_new_value(heap);
            matteValue_t buttonval = matte_heap_new_value(heap);
            matteValue_t eventVal = matte_heap_new_value(heap);
            
            double xcon, ycon;
            int w, h;
            SDL_GetWindowSize(sdl.window, &w, &h);
            
            
            
            matte_value_into_number(heap, &xval, (evt.button.x / (float) w) * ses_sdl_gl_get_render_width());
            matte_value_into_number(heap, &yval, (evt.button.y / (float) h) * ses_sdl_gl_get_render_height());
            matte_value_into_number(heap, &eventVal, evt.button.type == SDL_MOUSEBUTTONDOWN ? 3 : 4);

            int which;
            switch(evt.button.button) {
              case SDL_BUTTON_LEFT:   which = 0; break;
              case SDL_BUTTON_MIDDLE: which = 1; break;
              case SDL_BUTTON_RIGHT:  which = 2; break;
            }

            matte_value_into_number(heap, &buttonval, evt.button.button);



            matteValue_t namesArr[] = {event, x, y, button};
            matteValue_t valsArr[] = {eventVal, xval, yval, buttonval};                
            
            for(i = 0; i < len; ++i) {
                matteValue_t val = matte_array_at(sdl.main.inputs[SES_DEVICE__POINTER0].callbacks, matteValue_t, i);    
                if (val.binID == 0) continue;

                // for safety
                matteArray_t names = MATTE_ARRAY_CAST(namesArr, matteValue_t, 4);
                matteArray_t vals = MATTE_ARRAY_CAST(valsArr, matteValue_t, 4);

                matte_vm_call(sdl.vm, val, &vals, &names, NULL);
                
            }
            break;
          }


          case SDL_MOUSEWHEEL: {
          
          
            sdl.hasPointerScrollEvent = 1;            
            sdl.pointerScrollX = evt.wheel.x;
            sdl.pointerScrollY = evt.wheel.y;

            break;
          }

        
          case SDL_MOUSEMOTION: {
          
            sdl.hasPointerMotionEvent = 1;
            sdl.pointerX = evt.motion.x;
            sdl.pointerY = evt.motion.y;
            break;
          }
            
        }
        
        
        // frame update controlled by timer
        if (evt.type == SDL_USEREVENT) {
            if (sdl.main.updateFunc.binID) {
                matte_vm_call(
                    sdl.vm,
                    sdl.main.updateFunc,
                    matte_array_empty(),
                    matte_array_empty(),
                    NULL
                );
            }

            int i, len;

            // only process motion / scroll events on frame render
            // this groups them logically rather than clogging up the even queue.
            if (sdl.hasPointerMotionEvent) {
                sdl.hasPointerMotionEvent = 0;
                push_motion_callback();
            }
            if (sdl.hasPointerScrollEvent) {
                sdl.hasPointerScrollEvent = 0;
                push_scroll_callback();
            }

            ses_sdl_render();
            SDL_GL_SwapWindow(sdl.window);
        }

        // finally, check alarms
        ses_cartridge_poll_oscillators(ses.mainCart, SDL_GetTicks());

    }



    SDL_Delay(1);
    
    return 1;
}


int ses_api_main_loop(matte_t * m) {
    while(ses_api_update(m)) {}
    return 0;
}

/*
int ses_native_get_sprite_info(
    uint32_t index,
    
    float * x,
    float * y,
    float * rotation,
    float * scaleX,
    float * scaleY,
    float * centerX,
    float * centerY,
    int * layer,
    int * effect,
    int * enabled,

    uint32_t * palette,
    uint32_t * tile    
    
) {
    SES_Context * ctx = sdl.swapped ? &sdl.aux : &sdl.main;
    if (index >= SPRITE_COUNT_TOTAL) return 0;
    
    SES_Sprite * spr = &ctx->sprites[index];
    *x = spr->x;
    *y = spr->y;
    *rotation = spr->rotation;
    *scaleX = spr->scaleX;
    *scaleY = spr->scaleY;
    *centerX = spr->centerX;
    *centerY = spr->centerY;
    *layer = spr->layer;
    *effect = spr->effect;
    *enabled = spr->enabled;
    *palette = spr->palette;
    *tile = spr->tile;
    return 1;
}


int ses_native_get_tile_info(
    uint32_t tile,
    uint8_t * data
) {
    if (tile > TILE_ID_MAX) return 0;
    ses_sdl_gl_bind_tile(tile);
    
    int i;
    for(i = 0; i < 64; ++i) {
        data[i] = ses_sdl_gl_get_tile_pixel(i);
    }
    
    ses_sdl_gl_unbind_tile(tile);
    return 1;    
}


int ses_native_get_palette_info(
    uint32_t index,
    sesVector_t * data
) {
    SES_Context * ctx = sdl.swapped ? &sdl.aux : &sdl.main;
    if (index >= matte_array_get_size(ctx->palettes)) return 0;

    SES_Palette p = matte_array_at(ctx->palettes, SES_Palette, index);
    data[0] = p.back;
    data[1] = p.midBack;
    data[2] = p.midFront;
    data[3] = p.front;
    return 1;
}

static matteValue_t ses_sdl_get_calling_bank(matteVM_t * vm, matteValue_t fn, const matteValue_t * args, void * userData) {
    matteValue_t v = matte_heap_new_value(matte_vm_get_heap(vm));
    matte_value_into_number(matte_vm_get_heap(vm), &v, 0);
    return v;
}
*/


