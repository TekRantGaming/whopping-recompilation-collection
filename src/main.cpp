// The window: SDL2 + OpenGL 3.3 + Dear ImGui, like the TRG Launcher's
// standalone host, but drawing the collection's App (home screen, per game
// launchers) instead of a single launcher.
//
// Testing aid: --screenshot <file.bmp> renders a few frames in a hidden window,
// saves the last one and quits; --view home|game:<n>:<page>|install:<n>|settings
// picks what is shown, --hover <n> starts with that game focused, and
// --press a,down,rb... presses controller buttons one after another first.
// --release-server <url> asks that server for releases instead of GitHub
// (tools/fake_github.py), --test-download <n> downloads or updates game n and
// writes the result to download-test.txt.

#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <SDL_opengl.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "app.h"
#include "sounds.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl2.h"

namespace whopping {

// Fullscreen is a borderless window covering the display it is on (not SDL's fullscreen mode), so Windows
// dialogs such as the disc image picker open above it.
bool IsFullscreen(SDL_Window* win) { return (SDL_GetWindowFlags(win) & SDL_WINDOW_BORDERLESS) != 0; }

void SetFullscreen(SDL_Window* win, bool on) {
  int display = SDL_GetWindowDisplayIndex(win);
  if (display < 0) display = 0;
  SDL_Rect bounds{0, 0, 1600, 900}, usable{0, 0, 1600, 900};
  SDL_GetDisplayBounds(display, &bounds);
  SDL_GetDisplayUsableBounds(display, &usable);
  if (on) {
    SDL_SetWindowBordered(win, SDL_FALSE);
    SDL_SetWindowResizable(win, SDL_FALSE);
    SDL_SetWindowPosition(win, bounds.x, bounds.y);
    SDL_SetWindowSize(win, bounds.w, bounds.h);
  } else {
    const int w = std::min(1440, int(usable.w * 0.9f)), h = std::min(860, int(usable.h * 0.9f));
    SDL_SetWindowBordered(win, SDL_TRUE);
    SDL_SetWindowResizable(win, SDL_TRUE);
    SDL_SetWindowSize(win, w, h);
    SDL_SetWindowPosition(win, usable.x + (usable.w - w) / 2, usable.y + (usable.h - h) / 2);
  }
}

void ToggleFullscreen(SDL_Window* win) { SetFullscreen(win, !IsFullscreen(win)); }

}  // namespace whopping

namespace {

struct Gl {
  void(APIENTRY* Viewport)(GLint, GLint, GLsizei, GLsizei) = nullptr;
  void(APIENTRY* ClearColor)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
  void(APIENTRY* Clear)(GLbitfield) = nullptr;
  void(APIENTRY* GenTextures)(GLsizei, GLuint*) = nullptr;
  void(APIENTRY* DeleteTextures)(GLsizei, const GLuint*) = nullptr;
  void(APIENTRY* BindTexture)(GLenum, GLuint) = nullptr;
  void(APIENTRY* TexParameteri)(GLenum, GLenum, GLint) = nullptr;
  void(APIENTRY* PixelStorei)(GLenum, GLint) = nullptr;
  void(APIENTRY* TexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*) = nullptr;
  void(APIENTRY* ReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*) = nullptr;
  void(APIENTRY* ReadBuffer)(GLenum) = nullptr;

  template <typename F>
  static void Load(F& fn, const char* name) {
    fn = reinterpret_cast<F>(SDL_GL_GetProcAddress(name));
  }
  bool Init() {
    Load(Viewport, "glViewport");
    Load(ClearColor, "glClearColor");
    Load(Clear, "glClear");
    Load(GenTextures, "glGenTextures");
    Load(DeleteTextures, "glDeleteTextures");
    Load(BindTexture, "glBindTexture");
    Load(TexParameteri, "glTexParameteri");
    Load(PixelStorei, "glPixelStorei");
    Load(TexImage2D, "glTexImage2D");
    Load(ReadPixels, "glReadPixels");
    Load(ReadBuffer, "glReadBuffer");
    return Viewport && ClearColor && Clear && GenTextures && TexImage2D;
  }
};
Gl gl;

whopping::Texture Upload(int width, int height, const unsigned char* rgba) {
  whopping::Texture t;
  if (!rgba || width <= 0 || height <= 0) return t;
  GLuint tex = 0;
  gl.GenTextures(1, &tex);
  gl.BindTexture(GL_TEXTURE_2D, tex);
  gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, 0x812F);
  gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, 0x812F);
  gl.PixelStorei(GL_UNPACK_ALIGNMENT, 1);
  gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
  t.id = ImTextureID(tex);
  t.width = width;
  t.height = height;
  return t;
}

void FreeTexture(whopping::Texture& t) {
  if (!t) return;
  GLuint id = GLuint(t.id);
  gl.DeleteTextures(1, &id);
  t = whopping::Texture();
}

bool SaveBmp(const std::string& path, int w, int h, const std::vector<unsigned char>& rgba) {
  const int row = w * 3, pad = (4 - row % 4) % 4, size = (row + pad) * h;
  unsigned char hdr[54] = {'B', 'M'};
  auto put = [&](int o, int v) {
    for (int k = 0; k < 4; ++k) hdr[o + k] = static_cast<unsigned char>((v >> (8 * k)) & 255);
  };
  put(2, 54 + size);
  put(10, 54);
  put(14, 40);
  put(18, w);
  put(22, h);
  hdr[26] = 1;
  hdr[28] = 24;
  put(34, size);
  std::ofstream f(path, std::ios::binary);
  if (!f) return false;
  f.write(reinterpret_cast<char*>(hdr), 54);
  std::vector<unsigned char> line(row + pad, 0);
  for (int y = 0; y < h; ++y) {  // GL rows are bottom up, like BMP
    const unsigned char* s = rgba.data() + size_t(y) * w * 4;
    for (int x = 0; x < w; ++x) {
      line[x * 3] = s[x * 4 + 2];
      line[x * 3 + 1] = s[x * 4 + 1];
      line[x * 3 + 2] = s[x * 4];
    }
    f.write(reinterpret_cast<char*>(line.data()), line.size());
  }
  return bool(f);
}

}  // namespace

int main(int argc, char** argv) {
  whopping::AppOptions options;
  if (const char* server = std::getenv("WHOPPING_RELEASE_SERVER")) whopping::g_release_server = server;
  std::string screenshot;
  std::vector<whopping::dash::Btn> presses;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--screenshot" && i + 1 < argc) screenshot = argv[++i];
    else if (a == "--view" && i + 1 < argc) options.view = argv[++i];
    else if (a == "--hover" && i + 1 < argc) options.hover = std::atoi(argv[++i]);
    else if (a == "--offline") options.offline = true;
    else if (a == "--press" && i + 1 < argc) {
      static const char* names[] = {"up", "down", "left", "right", "a", "b", "x", "y", "lb", "rb", "start"};
      std::string list = argv[++i];
      size_t p = 0;
      while (p <= list.size()) {
        const size_t c = std::min(list.find(',', p), list.size());
        const std::string n = list.substr(p, c - p);
        for (int b = 0; b < int(std::size(names)); ++b)
          if (n == names[b]) presses.push_back(whopping::dash::Btn(b));
        p = c + 1;
      }
    }
    else if (a == "--frames" && i + 1 < argc) options.frames = std::atoi(argv[++i]);
    else if (a == "--test-play" && i + 1 < argc) {
      options.test_play = std::atoi(argv[++i]);
      options.offline = true;
    } else if (a == "--release-server" && i + 1 < argc) {
      whopping::g_release_server = argv[++i];
    } else if (a == "--test-download" && i + 1 < argc) {
      options.test_download = std::atoi(argv[++i]);
      options.offline = true;  // no art downloads or startup updates: only the job under test
    } else if (a == "--test-install" && i + 2 < argc) {
      options.test_install = std::atoi(argv[++i]);
      options.test_iso = argv[++i];
      options.offline = true;
    }
  }

#ifdef SDL_HINT_WINDOWS_DPI_SCALING
  SDL_SetHint(SDL_HINT_WINDOWS_DPI_SCALING, "1");
#endif
  SDL_SetHint(SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS, "0");
  SDL_SetMainReady();
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER) != 0) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "The Whopping Recompilation Collection", SDL_GetError(), nullptr);
    return 1;
  }
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_Rect usable = {0, 0, 1600, 900};
  SDL_GetDisplayUsableBounds(0, &usable);
  int ww = std::min(1440, int(float(usable.w) * 0.9f));
  int wh = std::min(860, int(float(usable.h) * 0.9f));
  Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
  // Fullscreen by default, borderless; F11 switches.
  const bool start_full = trg::TextFileSettings(whopping::LauncherDir() + "/whopping.txt").GetBool("fullscreen", true);
  if (!screenshot.empty()) {
    flags = SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN;
    ww = 1440;
    wh = 860;
  }
  SDL_Window* win = SDL_CreateWindow("The Whopping Recompilation Collection", SDL_WINDOWPOS_CENTERED,
                                     SDL_WINDOWPOS_CENTERED, ww, wh, flags);
  SDL_GLContext ctx = win ? SDL_GL_CreateContext(win) : nullptr;
  if (!ctx || !gl.Init()) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "The Whopping Recompilation Collection",
                             "OpenGL 3.3 is needed to show the launcher.", nullptr);
    return 1;
  }
  SDL_SetWindowMinimumSize(win, std::min(1024, ww), std::min(640, wh));
  if (screenshot.empty() && start_full) whopping::SetFullscreen(win, true);
  SDL_GL_MakeCurrent(win, ctx);
  SDL_GL_SetSwapInterval(1);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.LogFilename = nullptr;
  // No ImGui navigation: the dashboard keeps its own focus (dash.h).
  ImGui_ImplSDL2_InitForOpenGL(win, ctx);
  ImGui_ImplOpenGL3_Init("#version 330 core");

  whopping::g_free_texture = &FreeTexture;
  if (screenshot.empty()) whopping::sound::Init();  // no sounds from hidden test windows
  whopping::App app(options, &Upload, win);
  app.Start();

  bool quit = false;
  int frame = 0;
  double suppress_clicks_until = 0.0;
  while (!quit) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
      if ((ev.type == SDL_MOUSEBUTTONDOWN || ev.type == SDL_MOUSEBUTTONUP) && ImGui::GetTime() < suppress_clicks_until)
        continue;
      ImGui_ImplSDL2_ProcessEvent(&ev);
      if (ev.type == SDL_DROPFILE) {
        app.DropFile(ev.drop.file);
        SDL_free(ev.drop.file);
      }
      if (ev.type == SDL_QUIT) quit = true;
      if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_F11 && !ev.key.repeat) whopping::ToggleFullscreen(win);
      if (ev.type == SDL_WINDOWEVENT && ev.window.event == SDL_WINDOWEVENT_CLOSE &&
          ev.window.windowID == SDL_GetWindowID(win))
        quit = true;
      if (ev.type == SDL_WINDOWEVENT && ev.window.event == SDL_WINDOWEVENT_FOCUS_GAINED)
        suppress_clicks_until = ImGui::GetTime() + 0.25;
    }
    if (quit) break;
    if (screenshot.empty() && (SDL_GetWindowFlags(win) & SDL_WINDOW_MINIMIZED)) {
      app.Idle();
      SDL_Delay(30);
      continue;
    }
    if (frame >= 10 && frame % 6 == 0 && size_t(frame / 6 - 2) < presses.size())
      whopping::dash::Inject(presses[frame / 6 - 2]);
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
    if (!app.Frame()) quit = true;
    ImGui::Render();
    int dw = 0, dh = 0;
    SDL_GL_GetDrawableSize(win, &dw, &dh);
    gl.Viewport(0, 0, dw, dh);
    const ImVec4 bg = app.clear_color();
    gl.ClearColor(bg.x, bg.y, bg.z, 1.0f);
    gl.Clear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    if (!screenshot.empty() && ++frame >= options.frames) {
      std::vector<unsigned char> px(size_t(dw) * dh * 4);
      if (gl.ReadBuffer) gl.ReadBuffer(GL_BACK);
      gl.ReadPixels(0, 0, dw, dh, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
      SaveBmp(screenshot, dw, dh, px);
      quit = true;
    }
    SDL_GL_SwapWindow(win);
  }

  app.Stop();
  whopping::sound::Shutdown();
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext();
  SDL_GL_DeleteContext(ctx);
  SDL_DestroyWindow(win);
  SDL_Quit();
  return 0;
}
