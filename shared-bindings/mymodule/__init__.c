#include <stdint.h>

#include "py/obj.h"
#include "py/runtime.h"

#include "shared-bindings/mymodule/__init__.h"
#include "shared-bindings/mymodule/MyClass.h"

STATIC const mp_rom_map_elem_t mymodule_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_mymodule) },
    { MP_ROM_QSTR(MP_QSTR_MyClass), MP_ROM_PTR(&mymodule_myclass_type) },
};

STATIC MP_DEFINE_CONST_DICT(mymodule_module_globals, mymodule_module_globals_table);

const mp_obj_module_t mymodule_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t*)&mymodule_module_globals,
};
