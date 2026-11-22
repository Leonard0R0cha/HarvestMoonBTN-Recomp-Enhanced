/* The in-game overlay (F7): Dear ImGui (runtime/imgui), fed from SDL's events here and drawn by
 * platform_sdl.c (SDL renderer) or vk_renderer.cpp (Vulkan) from overlay_draw. The game is paused
 * while it is open (platform_sdl.c). Its font is ImGui's own, made once at the screen's size. */
#include "overlay.h"
#include "crt_params.h"
#include "imgui/imgui.h"
#include <SDL.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

static bool open_, ready, pause_ = true, peek_key, peek_pad;     /* peek: Tab or L2 held, the overlay hidden */
static bool opened;     /* just opened: its keyboard/controller cursor shown at once (else the first press only shows it) */
static char status[96];

/* the few words of the overlay, as the launcher's language (en, pt, es) */
enum { T_FILTER, T_OFF, T_SCAN, T_CRT, T_NEEDS_VK, T_RESET, T_SAVE, T_SAVED, T_NOT_SAVED, T_BACK, T_TYPE, T_SMOOTH,
       T_PAUSE, T_PEEK, T_MUSIC, T_SEASONS, T_SPRING, T_SUMMER, T_AUTUMN, T_WINTER, T_RAIN, T_MUSIC_NOTE,
       T_MUSIC_ONLINE, T_N };
static const char *const texts[3][T_N] = {
    { "CRT filter", "Off", "Scanlines", "CRT", "Needs Vulkan, which isn't available here.", "Reset", "Save",
      "Saved in hmbtn.ini", "Could not save hmbtn.ini", "F7 or Esc: back to the game",
      "Ctrl+click a slider to type a value.", "Smooth 3D edges", "Pause the game",
      "Hold Tab or L2: the whole picture", "Farm music (jukebox)", "The game's own", "Spring", "Summer",
      "Autumn", "Winter", "Rainy day",
      "Instead of a season's or the rain's, from the next time you come to the farm.",
      "Not in online co-op." },
    { "Filtro CRT", "Desligado", "Scanlines", "CRT", "Precisa de Vulkan, que n\xc3\xa3o est\xc3\xa1 dispon\xc3\xadvel aqui.",
      "Restaurar", "Salvar", "Salvo em hmbtn.ini", "N\xc3\xa3o foi poss\xc3\xadvel salvar o hmbtn.ini",
      "F7 ou Esc: voltar ao jogo", "Ctrl+clique num controle para digitar um valor.", "Bordas 3D suaves",
      "Pausar o jogo", "Segure Tab ou L2: a imagem inteira", "M\xc3\xbasica da fazenda (jukebox)",
      "A do jogo", "Primavera", "Ver\xc3\xa3o", "Outono", "Inverno", "Dia de chuva",
      "No lugar da de uma esta\xc3\xa7\xc3\xa3o ou da chuva, da pr\xc3\xb3xima vez que voc\xc3\xaa chegar \xc3\xa0 fazenda.",
      "N\xc3\xa3o no coop online." },
    { "Filtro CRT", "Apagado", "Scanlines", "CRT", "Necesita Vulkan, que no est\xc3\xa1 disponible aqu\xc3\xad.",
      "Restablecer", "Guardar", "Guardado en hmbtn.ini", "No se pudo guardar hmbtn.ini",
      "F7 o Esc: volver al juego", "Ctrl+clic en un control para escribir un valor.", "Bordes 3D suaves",
      "Pausar el juego", "Mant\xc3\xa9n Tab o L2: la imagen entera", "M\xc3\xbasica de la granja (jukebox)",
      "La del juego", "Primavera", "Verano", "Oto\xc3\xb1o", "Invierno", "D\xc3\xad" "a de lluvia",
      "En lugar de la de una estaci\xc3\xb3n o la de la lluvia, desde la pr\xc3\xb3xima vez que llegues a la granja.",
      "No en el coop online." },
};

static const char *T(int i)
{
    const char *l = config_language();
    return texts[!strcmp(l, "pt") ? 1 : !strcmp(l, "es") ? 2 : 0][i];
}

extern "C" void overlay_init(int font_px)
{
    if (ready)
        return;
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;       /* nothing of its own saved */
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
    io.BackendPlatformName = io.BackendRendererName = "hmbtn";
    ImFontConfig fc;
    fc.SizePixels = (float)font_px;
    io.Fonts->AddFontDefaultVector(&fc);
    unsigned char *px;
    int w, h;
    io.Fonts->GetTexDataAsRGBA32(&px, &w, &h);     /* one texture, made now (no texture updates) */
    io.Fonts->SetTexID((ImTextureID)1);
    ImGui::StyleColorsDark();
    ImGui::GetStyle().ScaleAllSizes(font_px / 15.0f);
    ready = true;
}

extern "C" int overlay_open(void)
{
    return open_;
}

extern "C" void overlay_show(int on)
{
    open_ = on && ready;
    peek_key = peek_pad = false;
    opened = open_;
    status[0] = 0;
}

extern "C" int overlay_pauses(void)
{
    return open_ && pause_ && platform_can_pause();
}

static ImGuiKey key_of(SDL_Scancode s)
{
    if (s >= SDL_SCANCODE_A && s <= SDL_SCANCODE_Z)
        return ImGuiKey(ImGuiKey_A + (s - SDL_SCANCODE_A));
    if (s >= SDL_SCANCODE_1 && s <= SDL_SCANCODE_9)
        return ImGuiKey(ImGuiKey_1 + (s - SDL_SCANCODE_1));
    switch (s)
    {
    case SDL_SCANCODE_0: return ImGuiKey_0;
    case SDL_SCANCODE_TAB: return ImGuiKey_Tab;
    case SDL_SCANCODE_LEFT: return ImGuiKey_LeftArrow;
    case SDL_SCANCODE_RIGHT: return ImGuiKey_RightArrow;
    case SDL_SCANCODE_UP: return ImGuiKey_UpArrow;
    case SDL_SCANCODE_DOWN: return ImGuiKey_DownArrow;
    case SDL_SCANCODE_PAGEUP: return ImGuiKey_PageUp;
    case SDL_SCANCODE_PAGEDOWN: return ImGuiKey_PageDown;
    case SDL_SCANCODE_HOME: return ImGuiKey_Home;
    case SDL_SCANCODE_END: return ImGuiKey_End;
    case SDL_SCANCODE_DELETE: return ImGuiKey_Delete;
    case SDL_SCANCODE_BACKSPACE: return ImGuiKey_Backspace;
    case SDL_SCANCODE_SPACE: return ImGuiKey_Space;
    case SDL_SCANCODE_RETURN: return ImGuiKey_Enter;
    case SDL_SCANCODE_KP_ENTER: return ImGuiKey_KeypadEnter;
    case SDL_SCANCODE_ESCAPE: return ImGuiKey_Escape;
    case SDL_SCANCODE_MINUS: return ImGuiKey_Minus;
    case SDL_SCANCODE_PERIOD: return ImGuiKey_Period;
    case SDL_SCANCODE_LCTRL: return ImGuiKey_LeftCtrl;
    case SDL_SCANCODE_RCTRL: return ImGuiKey_RightCtrl;
    case SDL_SCANCODE_LSHIFT: return ImGuiKey_LeftShift;
    case SDL_SCANCODE_RSHIFT: return ImGuiKey_RightShift;
    case SDL_SCANCODE_LALT: return ImGuiKey_LeftAlt;
    case SDL_SCANCODE_RALT: return ImGuiKey_RightAlt;
    default: return ImGuiKey_None;
    }
}

static ImGuiKey pad_key(int b)
{
    switch (b)
    {
    case SDL_CONTROLLER_BUTTON_A: return ImGuiKey_GamepadFaceDown;
    case SDL_CONTROLLER_BUTTON_B: return ImGuiKey_GamepadFaceRight;
    case SDL_CONTROLLER_BUTTON_X: return ImGuiKey_GamepadFaceLeft;
    case SDL_CONTROLLER_BUTTON_Y: return ImGuiKey_GamepadFaceUp;
    case SDL_CONTROLLER_BUTTON_DPAD_UP: return ImGuiKey_GamepadDpadUp;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return ImGuiKey_GamepadDpadDown;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return ImGuiKey_GamepadDpadLeft;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return ImGuiKey_GamepadDpadRight;
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return ImGuiKey_GamepadL1;
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return ImGuiKey_GamepadR1;
    case SDL_CONTROLLER_BUTTON_START: return ImGuiKey_GamepadStart;
    case SDL_CONTROLLER_BUTTON_BACK: return ImGuiKey_GamepadBack;
    default: return ImGuiKey_None;
    }
}

extern "C" void overlay_event(const void *e, float mx, float my)
{
    if (!open_)
        return;
    const SDL_Event &ev = *(const SDL_Event *)e;
    ImGuiIO &io = ImGui::GetIO();
    switch (ev.type)
    {
    case SDL_MOUSEMOTION:
        io.AddMousePosEvent(ev.motion.x * mx, ev.motion.y * my);
        break;
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
        if (ev.button.button >= 1 && ev.button.button <= 3)     /* left, middle, right */
            io.AddMouseButtonEvent(ev.button.button == 1 ? 0 : ev.button.button == 3 ? 1 : 2,
                                   ev.type == SDL_MOUSEBUTTONDOWN);
        break;
    case SDL_MOUSEWHEEL:
        io.AddMouseWheelEvent((float)ev.wheel.x, (float)ev.wheel.y);
        break;
    case SDL_TEXTINPUT:
        io.AddInputCharactersUTF8(ev.text.text);
        break;
    case SDL_KEYDOWN:
    case SDL_KEYUP:
    {
        bool down = ev.type == SDL_KEYDOWN;
        SDL_Scancode s = ev.key.keysym.scancode;
        /* F7, or Esc unless a value is being typed: back to the game */
        if (down && !ev.key.repeat && (s == SDL_SCANCODE_F7 || (s == SDL_SCANCODE_ESCAPE && !io.WantTextInput)))
        {
            overlay_show(0);
            return;
        }
        if (s == SDL_SCANCODE_TAB)      /* held: the overlay hidden, to see the whole picture */
        {
            peek_key = down;
            return;
        }
        io.AddKeyEvent(ImGuiMod_Ctrl, (ev.key.keysym.mod & KMOD_CTRL) != 0);
        io.AddKeyEvent(ImGuiMod_Shift, (ev.key.keysym.mod & KMOD_SHIFT) != 0);
        io.AddKeyEvent(ImGuiMod_Alt, (ev.key.keysym.mod & KMOD_ALT) != 0);
        ImGuiKey k = key_of(s);
        if (k != ImGuiKey_None)
            io.AddKeyEvent(k, down);
        break;
    }
    case SDL_CONTROLLERBUTTONDOWN:
    case SDL_CONTROLLERBUTTONUP:
    {
        ImGuiKey k = pad_key(ev.cbutton.button);
        if (k != ImGuiKey_None)
            io.AddKeyEvent(k, ev.type == SDL_CONTROLLERBUTTONDOWN);
        break;
    }
    case SDL_CONTROLLERAXISMOTION:
        if (ev.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT)   /* L2 held: as Tab */
            peek_pad = ev.caxis.value > 16000;
        if (ev.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX || ev.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY)
        {
            float v = ev.caxis.value / 32767.0f, a = fabsf(v) < 0.25f ? 0.0f : fabsf(v);
            bool x = ev.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX;
            io.AddKeyAnalogEvent(x ? ImGuiKey_GamepadLStickLeft : ImGuiKey_GamepadLStickUp, v < 0 && a > 0, v < 0 ? a : 0);
            io.AddKeyAnalogEvent(x ? ImGuiKey_GamepadLStickRight : ImGuiKey_GamepadLStickDown, v > 0 && a > 0, v > 0 ? a : 0);
        }
        break;
    case SDL_WINDOWEVENT:
        if (ev.window.event == SDL_WINDOWEVENT_FOCUS_LOST)
            io.AddFocusEvent(false);
        else if (ev.window.event == SDL_WINDOWEVENT_FOCUS_GAINED)
            io.AddFocusEvent(true);
        break;
    }
}

/* the jukebox (runtime/jukebox.c): the farm's music of any season, or of a rainy day */
static void music_page()
{
    ImGui::SeparatorText(T(T_MUSIC));
    bool can = platform_can_pause() != 0;     /* online co-op: the two games would differ */
    ImGui::BeginDisabled(!can);
    int now = can ? jukebox_choice() : 0;
    /* the songs (0 the game's own) and their names: the game's own and the rain's, then the seasons */
    const int songs[6] = { 0, 22, 1, 2, 3, 4 };
    const int names[6] = { T_SEASONS, T_RAIN, T_SPRING, T_SUMMER, T_AUTUMN, T_WINTER };
    for (int m = 0; m < 6; m++)
    {
        if (m != 0 && m != 2)
            ImGui::SameLine();
        if (ImGui::RadioButton(T(names[m]), now == songs[m]) && songs[m] != now)
            jukebox_choose(songs[m]);
    }
    ImGui::EndDisabled();
    ImGui::TextDisabled("%s", T(can ? T_MUSIC_NOTE : T_MUSIC_ONLINE));
}

/* the CRT filter: which, and the chosen one's settings */
static void crt_page()
{
    int mode = platform_crt_mode();
    ImGui::SeparatorText(T(T_FILTER));
    const int names[3] = { T_OFF, T_SCAN, T_CRT };
    for (int m = 0; m < 3; m++)
    {
        if (m)
            ImGui::SameLine();
        if (ImGui::RadioButton(T(names[m]), mode == m) && m != mode)
        {
            platform_crt(m);
            if (m && !platform_crt_mode())
                snprintf(status, sizeof status, "%s", T(T_NEEDS_VK));
            mode = platform_crt_mode();
        }
    }
    if (mode)
    {
        int s = mode - 1, n = crt_param_count(s);
        float *v = platform_crt_params(s);
        bool changed = false;
        ImGui::TextDisabled("%s", s ? "crt-lottes (Timothy Lottes)" : "crt-hyllian (Hyllian)");
        ImGui::PushItemWidth(ImGui::GetFontSize() * 14);
        for (int i = 0; i < n; i++)
        {
            const CrtParam &p = crt_params[s][i];
            const char *fmt = p.step >= 1.0f ? "%.0f" : p.step >= 0.1f ? "%.1f" : p.step >= 0.01f ? "%.2f" : "%.3f";
            float was = v[i];
            if (ImGui::SliderFloat(p.label, &v[i], p.min, p.max, fmt))
            {
                /* on its steps, as RetroArch; the arrows and the d-pad nudge by less: a step each */
                float s = p.min + roundf((v[i] - p.min) / p.step) * p.step;
                v[i] = s != was ? s : v[i] > was ? was + p.step : v[i] < was ? was - p.step : was;
                v[i] = v[i] < p.min ? p.min : v[i] > p.max ? p.max : v[i];
                changed = true;
            }
        }
        ImGui::PopItemWidth();
        bool smooth = *platform_crt_smooth() != 0;
        if (ImGui::Checkbox(T(T_SMOOTH), &smooth))
        {
            *platform_crt_smooth() = smooth;
            changed = true;
        }
        if (ImGui::Button(T(T_RESET)))
        {
            for (int i = 0; i < n; i++)
                v[i] = crt_params[s][i].def;
            changed = true;
        }
        if (changed)
        {
            platform_crt(mode);
            status[0] = 0;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(T(T_SAVE)))
    {
        static const char *const modes[3] = { "off", "scanlines", "crt" };
        bool ok = config_save("video", "crt", modes[mode]) != 0;
        char key[64], val[32];
        ok = ok && config_save("crt", "crt_smooth", *platform_crt_smooth() ? "1" : "0");
        for (int s = 1; s >= 0 && ok; s--)     /* (new lines go to the top of [crt]: from the last) */
            for (int i = crt_param_count(s) - 1; i >= 0 && ok; i--)
            {
                snprintf(key, sizeof key, "%s%s", crt_shader_key[s], crt_params[s][i].name);
                snprintf(val, sizeof val, "%g", platform_crt_params(s)[i]);
                ok = config_save("crt", key, val) != 0;
            }
        snprintf(status, sizeof status, "%s", T(ok ? T_SAVED : T_NOT_SAVED));
    }
}

extern "C" void overlay_frame(int w, int h, float seconds)
{
    if (!open_ || w <= 0 || h <= 0)
        return;
    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)w, (float)h);
    io.DeltaTime = seconds > 0.0f && seconds < 1.0f ? seconds : 1.0f / 60.0f;
    ImGui::NewFrame();
    /* at the left, see-through: most of the picture stays in sight */
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetFontSize(), h * 0.5f), ImGuiCond_Appearing, ImVec2(0.0f, 0.5f));
    ImGui::SetNextWindowBgAlpha(0.75f);
    bool keep = true;
    if (ImGui::Begin("HMBTN ReFarmed", &keep, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse))
    {
        if (opened)
        {
            ImGui::SetNavCursorVisible(true);
            opened = false;
        }
        bool can = platform_can_pause() != 0, p = pause_ && can;
        ImGui::BeginDisabled(!can);     /* online co-op: the game goes on */
        if (ImGui::Checkbox(T(T_PAUSE), &p))
            pause_ = p;
        ImGui::SetItemDefaultFocus();   /* keyboard and controller start here */
        ImGui::EndDisabled();
        crt_page();
        music_page();
        if (status[0])
            ImGui::TextUnformatted(status);
        ImGui::Separator();
        ImGui::TextDisabled("%s", T(T_BACK));
        ImGui::TextDisabled("%s", T(T_PEEK));
        ImGui::TextDisabled("%s", T(T_TYPE));
    }
    ImGui::End();
    ImGui::Render();
    if (!keep)
        overlay_show(0);
}

extern "C" void overlay_font(const unsigned char **rgba, int *w, int *h)
{
    unsigned char *px;
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&px, w, h);
    *rgba = px;
}

extern "C" int overlay_sizes(int *nvtx, int *nidx)
{
    ImDrawData *d = open_ && !peek_key && !peek_pad ? ImGui::GetDrawData() : nullptr;
    *nvtx = d ? d->TotalVtxCount : 0;
    *nidx = d ? d->TotalIdxCount : 0;
    return *nidx > 0;
}

extern "C" void overlay_draw(void *ctx, OverlayList list, OverlayCmd cmd)
{
    ImDrawData *d = ImGui::GetDrawData();
    if (!d)
        return;
    for (const ImDrawList *l : d->CmdLists)
    {
        list(ctx, l->VtxBuffer.Data, l->VtxBuffer.Size, l->IdxBuffer.Data, l->IdxBuffer.Size);
        for (const ImDrawCmd &c : l->CmdBuffer)
            if (!c.UserCallback && c.ElemCount)
            {
                const float clip[4] = { c.ClipRect.x, c.ClipRect.y, c.ClipRect.z, c.ClipRect.w };
                cmd(ctx, clip, (int)c.IdxOffset, (int)c.VtxOffset, (int)c.ElemCount);
            }
    }
}
