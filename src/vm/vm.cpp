//
//  ZEPTO-8 — Fantasy console emulator
//
//  Copyright © 2016—2018 Sam Hocevar <sam@hocevar.net>
//
//  This program is free software. It comes without any warranty, to
//  the extent permitted by applicable law. You can redistribute it
//  and/or modify it under the terms of the Do What the Fuck You Want
//  to Public License, Version 2, as published by the WTFPL Task Force.
//  See http://www.wtfpl.net/ for more details.
//

#include <lol/engine.h>

#include "vm.h"
#include "z8lua/lualib.h"

// FIXME: activate this one day, when we use Lua 5.3
#define HAVE_LUA_GETEXTRASPACE 0

namespace z8
{

using lol::msg;

/* Helper to dispatch C++ functions to Lua C bindings */
typedef int (vm::*api_func)(lua_State *);

template<api_func f> static int dispatch(lua_State *l)
{
#if HAVE_LUA_GETEXTRASPACE
    vm *that = *static_cast<vm**>(lua_getextraspace(l));
#else
    lua_getglobal(l, "\x01");
    vm *that = (vm *)lua_touserdata(l, -1);
    lua_remove(l, -1);
#endif
    return ((*that).*f)(l);
}

vm::vm()
  : m_instructions(0)
{
    m_lua = luaL_newstate();
    lua_atpanic(m_lua, &vm::panic_hook);
    luaL_openlibs(m_lua);

    // Store a pointer to us in global state
#if HAVE_LUA_GETEXTRASPACE
    *static_cast<vm**>(lua_getextraspace(m_lua)) = this;
#else
    lua_pushlightuserdata(m_lua, this);
    lua_setglobal(m_lua, "\x01");
#endif

    static const luaL_Reg zepto8lib[] =
    {
        { "run",      &dispatch<&vm::api_run> },
        { "menuitem", &dispatch<&vm::api_menuitem> },
        { "reload",   &dispatch<&vm::api_reload> },
        { "peek",     &dispatch<&vm::api_peek> },
        { "peek4",    &dispatch<&vm::api_peek4> },
        { "poke",     &dispatch<&vm::api_poke> },
        { "poke4",    &dispatch<&vm::api_poke4> },
        { "memcpy",   &dispatch<&vm::api_memcpy> },
        { "memset",   &dispatch<&vm::api_memset> },
        { "stat",     &dispatch<&vm::api_stat> },
        { "printh",   &vm::api_printh },
        { "extcmd",   &dispatch<&vm::api_extcmd> },

        { "_update_buttons", &dispatch<&vm::api_update_buttons> },
        { "btn",  &dispatch<&vm::api_btn> },
        { "btnp", &dispatch<&vm::api_btnp> },

        { "cursor", &dispatch<&vm::api_cursor> },
        { "print",  &dispatch<&vm::api_print> },

        { "rnd",   &dispatch<&vm::api_rnd> },
        { "srand", &dispatch<&vm::api_srand> },

        { "camera",   &dispatch<&vm::api_camera> },
        { "circ",     &dispatch<&vm::api_circ> },
        { "circfill", &dispatch<&vm::api_circfill> },
        { "clip",     &dispatch<&vm::api_clip> },
        { "cls",      &dispatch<&vm::api_cls> },
        { "color",    &dispatch<&vm::api_color> },
        { "fillp",    &dispatch<&vm::api_fillp> },
        { "fget",     &dispatch<&vm::api_fget> },
        { "fset",     &dispatch<&vm::api_fset> },
        { "line",     &dispatch<&vm::api_line> },
        { "map",      &dispatch<&vm::api_map> },
        { "mget",     &dispatch<&vm::api_mget> },
        { "mset",     &dispatch<&vm::api_mset> },
        { "pal",      &dispatch<&vm::api_pal> },
        { "palt",     &dispatch<&vm::api_palt> },
        { "pget",     &dispatch<&vm::api_pget> },
        { "pset",     &dispatch<&vm::api_pset> },
        { "rect",     &dispatch<&vm::api_rect> },
        { "rectfill", &dispatch<&vm::api_rectfill> },
        { "sget",     &dispatch<&vm::api_sget> },
        { "sset",     &dispatch<&vm::api_sset> },
        { "spr",      &dispatch<&vm::api_spr> },
        { "sspr",     &dispatch<&vm::api_sspr> },

        { "music", &dispatch<&vm::api_music> },
        { "sfx",   &dispatch<&vm::api_sfx> },

        { "time", &dispatch<&vm::api_time> },

        { "__cartdata", &dispatch<&vm::private_cartdata> },

        { nullptr, nullptr },
    };

    lua_pushglobaltable(m_lua);
    luaL_setfuncs(m_lua, zepto8lib, 0);

    // Automatically yield every 1000 instructions
    lua_sethook(m_lua, &vm::instruction_hook, LUA_MASKCOUNT, 1000);

    // Load font
    m_font.load("src/data/font.png");

    // Clear memory
    ::memset(&m_ram, 0, sizeof(m_ram));

    // Initialize Zepto8 runtime
    char const *filename = "src/data/zepto8.lua";
    std::string s;
    int status = LUA_ERRFILE;
    for (auto const &candidate : lol::sys::get_path_list(filename))
    {
        lol::File f;
        f.Open(candidate, lol::FileAccess::Read);
        if (f.IsValid())
        {
            msg::debug("loading Lua file %s\n", candidate.c_str());
            s = f.ReadString();
            f.Close();
            status = LUA_OK;
            break;
        }
    }

    if (status == LUA_ERRFILE)
    {
        msg::error("could not load %s\n", filename);
        lol::Abort();
    }

    status = luaL_dostring(m_lua, s.c_str());

    if (status != LUA_OK)
    {
        char const *message = lua_tostring(m_lua, -1);
        msg::error("error %d loading %s: %s\n", status, s.c_str(), message);
        lua_pop(m_lua, 1);
        lol::Abort();
    }
}

vm::~vm()
{
}

int vm::panic_hook(lua_State* l)
{
    char const *message = lua_tostring(l, -1);
    msg::error("%s\n", message);
    lol::Abort();
    return 0;
}

void vm::instruction_hook(lua_State *l, lua_Debug *)
{
#if HAVE_LUA_GETEXTRASPACE
    vm *that = *static_cast<vm**>(lua_getextraspace(l));
#else
    lua_getglobal(l, "\x01");
    vm *that = (vm *)lua_touserdata(l, -1);
    lua_remove(l, -1);
#endif

    // The value 135000 was found using trial and error, but it causes
    // side effects in lots of cases. Use 300000 instead.
    that->m_instructions += 1000;
    if (that->m_instructions >= 300000)
        lua_yield(l, 0);
}

void vm::load(char const *name)
{
    m_cart.load(name);
}

void vm::run()
{
    // Start the cartridge!
    int status = luaL_dostring(m_lua, "run()");
    if (status == 1)
    {
        msg::error("Lua error %s\n", lua_tostring(m_lua, -1));
        lua_pop(m_lua, 1);
    }
}

void vm::step(float seconds)
{
    UNUSED(seconds);

    lua_getglobal(m_lua, "_z8");
    lua_getfield(m_lua, -1, "tick");
    lua_pcall(m_lua, 0, 0, 0);
    lua_remove(m_lua, -1);

    m_instructions = 0;
}

void vm::button(int index, int state)
{
    m_buttons[1][index] = state;
}

void vm::mouse(lol::ivec2 coords, int buttons)
{
    m_mouse.x = (double)coords.x;
    m_mouse.y = (double)coords.y;
    m_mouse.b = (double)buttons;
}

//
// System
//

int vm::api_run(lua_State *l)
{
    // Initialise VM state (TODO: check what else to init)
    ::memset(m_buttons, 0, sizeof(m_buttons));

    // Load cartridge code and call _z8.run() on it
    lua_getglobal(l, "_z8");
    lua_getfield(l, -1, "run");
    luaL_loadstring(l, m_cart.get_lua().c_str());
    lua_pcall(l, 1, 0, 0);

    return 0;
}

int vm::api_menuitem(lua_State *l)
{
    UNUSED(l);
    msg::info("z8:stub:menuitem\n");
    return 0;
}

int vm::api_reload(lua_State *l)
{
    int dst = 0, src = 0, size = offsetof(memory, code);

    // PICO-8 fully reloads the cartridge if not all arguments are passed
    if (!lua_isnone(l, 3))
    {
        dst = (int)lua_tonumber(l, 1) & 0xffff;
        src = (int)lua_tonumber(l, 2) & 0xffff;
        size = (int)lua_tonumber(l, 3);
    }

    if (size <= 0)
        return 0;

    size = size & 0xffff;

    // Attempting to write outside the memory area raises an error. Everything
    // else seems legal, especially reading from anywhere.
    if (dst + size > (int)sizeof(m_ram))
        return luaL_error(l, "bad memory access");

    // If reading from after the cart, fill that part with zeroes
    if (src > (int)offsetof(memory, code))
    {
        int amount = lol::min(size, (int)sizeof(m_ram) - src);
        ::memset(&m_ram[dst], 0, amount);
        dst += amount;
        src = (src + amount) & 0xffff;
        size -= amount;
    }

    // Now copy possibly legal data
    int amount = lol::min(size, (int)offsetof(memory, code) - src);
    ::memcpy(&m_ram[dst], &m_cart.get_rom()[src], amount);
    dst += amount;
    size -= amount;

    // If there is anything left to copy, it’s zeroes again
    ::memset(&m_ram[dst], 0, size);

    return 0;
}

int vm::api_peek(lua_State *l)
{
    // Note: peek() is the same as peek(0)
    int addr = (int)lua_tonumber(l, 1);
    if (addr < 0 || addr >= (int)sizeof(m_ram))
        return 0;

    lua_pushnumber(l, m_ram[addr]);
    return 1;
}

int vm::api_peek4(lua_State *l)
{
    int addr = (int)lua_tonumber(l, 1) & 0xffff;
    int32_t bits = 0;
    for (int i = 0; i < 4; ++i)
    {
        /* This code handles partial reads by adding zeroes */
        if (addr + i < (int)sizeof(m_ram))
            bits |= m_ram[addr + i] << (8 * i);
        else if (addr + i >= (int)sizeof(m_ram))
            bits |= m_ram[addr + i - (int)sizeof(m_ram)] << (8 * i);
    }

    lua_pushnumber(l, fix32::frombits(bits));
    return 1;
}

int vm::api_poke(lua_State *l)
{
    // Note: poke() is the same as poke(0, 0)
    int addr = (int)lua_tonumber(l, 1);
    int val = (int)lua_tonumber(l, 2);
    if (addr < 0 || addr > (int)sizeof(m_ram) - 1)
        return luaL_error(l, "bad memory access");

    m_ram[addr] = (uint8_t)val;
    return 0;
}

int vm::api_poke4(lua_State *l)
{
    // Note: poke4() is the same as poke(0, 0)
    int addr = (int)lua_tonumber(l, 1);
    if (addr < 0 || addr > (int)sizeof(m_ram) - 4)
        return luaL_error(l, "bad memory access");

    uint32_t x = (uint32_t)lua_tonumber(l, 2).bits();
    m_ram[addr + 0] = x;
    m_ram[addr + 1] = x >> 8;
    m_ram[addr + 2] = x >> 16;
    m_ram[addr + 3] = x >> 24;

    return 0;
}

int vm::api_memcpy(lua_State *l)
{
    int dst = (int)lua_tonumber(l, 1);
    int src = (int)lua_tonumber(l, 2) & 0xffff;
    int size = (int)lua_tonumber(l, 3);

    if (size <= 0)
        return 0;

    size = size & 0xffff;

    // Attempting to write outside the memory area raises an error. Everything
    // else seems legal, especially reading from anywhere.
    if (dst < 0 || dst + size > (int)sizeof(m_ram))
    {
        msg::info("z8:segv:memcpy(0x%x,0x%x,0x%x)\n", src, dst, size);
        return luaL_error(l, "bad memory access");
    }

    // If source is outside main memory, part of the operation will be
    // memset(0). But we delay the operation in case the source and the
    // destination overlap.
    int delayed_dst = dst, delayed_size = 0;
    if (src > (int)sizeof(m_ram))
    {
        delayed_size = lol::min(size, (int)sizeof(m_ram) - src);
        dst += delayed_size;
        src = (src + delayed_size) & 0xffff;
        size -= delayed_size;
    }

    // Now copy possibly legal data
    int amount = lol::min(size, (int)sizeof(m_ram) - src);
    memmove(&m_ram[dst], &m_ram[src], amount);
    dst += amount;
    size -= amount;

    // Fill possible zeroes we saved before, and if there is still something
    // to copy, it’s zeroes again
    if (delayed_size)
        ::memset(&m_ram[delayed_dst], 0, delayed_size);
    if (size)
        ::memset(&m_ram[dst], 0, size);

    return 0;
}

int vm::api_memset(lua_State *l)
{
    int dst = (int)lua_tonumber(l, 1);
    int val = (int)lua_tonumber(l, 2) & 0xff;
    int size = (int)lua_tonumber(l, 3);

    if (size <= 0)
        return 0;

    size = size & 0xffff;

    if (dst < 0 || dst + size > (int)sizeof(m_ram))
    {
        msg::info("z8:segv:memset(0x%x,0x%x,0x%x)\n", dst, val, size);
        return luaL_error(l, "bad memory access");
    }

    ::memset(&m_ram[dst], val, size);

    return 0;
}

int vm::api_stat(lua_State *l)
{
    int id = (int)lua_tonumber(l, 1);
    fix32 ret(0.0);

    if (id == 0)
    {
        // Perform a GC to avoid accounting for short lifespan objects.
        // Not sure about the performance cost of this.
        lua_gc(l, LUA_GCCOLLECT, 0);

        // From the PICO-8 documentation:
        // x:0 returns current Lua memory useage (0..1024MB)
        int32_t bits = ((int)lua_gc(l, LUA_GCCOUNT, 0) << 16)
                     + ((int)lua_gc(l, LUA_GCCOUNTB, 0) << 6);
        ret = fix32::frombits(bits);
    }
    else if (id == 1)
    {
        // From the PICO-8 documentation:
        // x:1 returns cpu useage for last frame (1.0 means 100% at 30fps)
        // TODO
    }
    else if (id >= 16 && id <= 19)
    {
        // undocumented parameters for stat(n):
        // 16..19: the sfx currently playing on each channel or -1 for none
        ret = fix32(m_channels[id - 16].m_sfx);
    }
    else if (id >= 20 && id <= 23)
    {
        // undocumented parameters for stat(n):
        // 20..23: the currently playing row number (0..31) or -1 for none
        // TODO
    }
    else if (id >= 32 && id <= 34 && m_ram.draw_state.mouse_flag == 1)
    {
        // undocumented mouse support
        ret = id == 32 ? m_mouse.x
            : id == 33 ? m_mouse.y
            : m_mouse.b;
    }

    lua_pushnumber(l, ret);
    return 1;
}

int vm::api_printh(lua_State *l)
{
    char const *str;
    if (lua_isnoneornil(l, 1))
        str = "false";
    else if (lua_isstring(l, 1))
        str = lua_tostring(l, 1);
    else
        str = lua_toboolean(l, 1) ? "true" : "false";

    fprintf(stdout, "%s\n", str);
    fflush(stdout);

    return 0;
}

int vm::api_extcmd(lua_State *l)
{
    char const *str;
    if (lua_isnoneornil(l, 1))
        str = "false";
    else if (lua_isstring(l, 1))
        str = lua_tostring(l, 1);
    else
        str = lua_toboolean(l, 1) ? "true" : "false";

    if (strcmp("label", str) == 0
         || strcmp("screen", str) == 0
         || strcmp("rec", str) == 0
         || strcmp("video", str) == 0)
        msg::info("z8:stub:extcmd(%s)\n", str);

    return 0;
}

//
// I/O
//

int vm::api_update_buttons(lua_State *l)
{
    UNUSED(l);

    // Update button state
    for (int i = 0; i < 64; ++i)
    {
        if (m_buttons[1][i])
            ++m_buttons[0][i];
        else
            m_buttons[0][i] = 0;
    }

    return 0;
}

int vm::api_btn(lua_State *l)
{
    if (lua_isnone(l, 1))
    {
        int bits = 0;
        for (int i = 0; i < 16; ++i)
            bits |= m_buttons[0][i] ? 1 << i : 0;
        lua_pushnumber(l, bits);
    }
    else
    {
        int index = (int)lua_tonumber(l, 1) + 8 * (int)lua_tonumber(l, 2);
        lua_pushboolean(l, m_buttons[0][index]);
    }

    return 1;
}

int vm::api_btnp(lua_State *l)
{
    auto was_pressed = [](int i)
    {
        // “Same as btn() but only true when the button was not pressed the last frame”
        if (i == 1)
            return true;
        // “btnp() also returns true every 4 frames after the button is held for 15 frames.”
        if (i > 15 && i % 4 == 0)
            return true;
        return false;
    };

    if (lua_isnone(l, 1))
    {
        int bits = 0;
        for (int i = 0; i < 16; ++i)
            bits |= was_pressed(m_buttons[0][i]) ? 1 << i : 0;
        lua_pushnumber(l, bits);
    }
    else
    {
        int index = (int)lua_tonumber(l, 1) + 8 * (int)lua_tonumber(l, 2);
        lua_pushboolean(l, was_pressed(m_buttons[0][index]));
    }

    return 1;
}

//
// Deprecated
//

int vm::api_time(lua_State *l)
{
    lua_pushnumber(l, (double)m_timer.poll());
    return 1;
}

} // namespace z8

