#define SDL_MAIN_USE_CALLBACKS 1
#include <cstdio>
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3_image/SDL_image.h>

// Platform-specific OpenGL headers
#if defined(__ANDROID__) || defined(__EMSCRIPTEN__)
#include <GLES3/gl3.h>
#else
// Desktop (Windows MinGW & Ubuntu Linux)
#include <glad/glad.h>
#endif

#if defined(__EMSCRIPTEN__)
#include <emscripten/html5.h>
#endif

// Anisotropic filtering extension definitions
#ifndef GL_EXT_texture_filter_anisotropic
#define GL_EXT_texture_filter_anisotropic 1
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#endif

// =============================================================================
// Emscripten Log Output Callback
// =============================================================================
#if defined(__EMSCRIPTEN__)
static void SDLCALL EmscriptenLogCallback(void *userdata, int category, SDL_LogPriority priority, const char *message)
{
    (void)userdata;
    (void)category;

    if (priority >= SDL_LOG_PRIORITY_ERROR)
    {
        fprintf(stderr, "%s\n", message);
    }
    else
    {
        fprintf(stdout, "%s\n", message);
    }
}
#endif

// =============================================================================
// Shaders (Combined for Android, Wasm, Windows, and Linux)
// =============================================================================
#if defined(__ANDROID__) || defined(__EMSCRIPTEN__)
#define GLSL_VERSION "#version 300 es\n"
#define GLSL_PRECISION "precision mediump float;\n"
#else
#define GLSL_VERSION "#version 330 core\n"
#define GLSL_PRECISION ""
#endif

static const char *vertexShaderSource =
    GLSL_VERSION
    R"(
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aTexCoord;

out vec2 TexCoord;

void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
    TexCoord = aTexCoord;
}
)";

static const char *fragmentShaderSource =
    GLSL_VERSION GLSL_PRECISION
    R"(
out vec4 FragColor;
in vec2 TexCoord;

uniform sampler2D ourTexture;

void main() {
    FragColor = texture(ourTexture, TexCoord);
}
)";

// =============================================================================
// Helper: Load Texture from WebP / Image Stream
// =============================================================================
static GLuint createTexture(const char *path)
{
    SDL_IOStream *io = SDL_IOFromFile(path, "rb");
    if (!io)
    {
        SDL_Log("Failed to open file stream for %s: %s", path, SDL_GetError());
        return 0;
    }

    SDL_Surface *surface = IMG_Load_IO(io, 1);
    if (!surface)
    {
        SDL_Log("Image loading failed: %s", SDL_GetError());
        return 0;
    }

    SDL_Surface *optimizedSurface = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_ABGR8888);
    SDL_DestroySurface(surface);

    if (!optimizedSurface)
    {
        SDL_Log("Surface conversion failed: %s", SDL_GetError());
        return 0;
    }

    GLuint textureID;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    if (SDL_GL_ExtensionSupported("GL_EXT_texture_filter_anisotropic"))
    {
        float maxAnisotropy = 0.0f;
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAnisotropy);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, maxAnisotropy);
        SDL_Log("Anisotropic filtering enabled. Max: %.1f", maxAnisotropy);
    }
    else
    {
        SDL_Log("Anisotropic filtering not supported.");
    }

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, optimizedSurface->w, optimizedSurface->h,
        0, GL_RGBA, GL_UNSIGNED_BYTE, optimizedSurface->pixels);
    glGenerateMipmap(GL_TEXTURE_2D);

    SDL_DestroySurface(optimizedSurface);

    return textureID;
}

// =============================================================================
// Application State
// =============================================================================
struct AppState
{
    SDL_Window *window = nullptr;
    SDL_GLContext glContext = nullptr;
    GLuint texture = 0;
    GLuint shaderProgram = 0;
    GLuint VAO = 0;
    GLuint VBO = 0;
};

// =============================================================================
// Render a Single Frame (Reused in both main loop and window resize)
// =============================================================================
static void renderFrame(AppState *state)
{
    if (!state || !state->window)
        return;

    int display_w = 0;
    int display_h = 0;

#if defined(__EMSCRIPTEN__)
    emscripten_get_canvas_element_size("#canvas", &display_w, &display_h);
#else
    SDL_GetWindowSizeInPixels(state->window, &display_w, &display_h);
#endif

    if (display_w <= 0 || display_h <= 0)
        return;

    // Update glViewport
    glViewport(0, 0, display_w, display_h);

    glClearColor(0.2f, 0.25f, 0.3f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    if (state->texture)
    {
        glUseProgram(state->shaderProgram);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, state->texture);
        glBindVertexArray(state->VAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }

    SDL_GL_SwapWindow(state->window);
}

// =============================================================================
// Direct Browser Resize Callback (Invoked DIRECTLY by the browser, like in Three.js)
// =============================================================================
#if defined(__EMSCRIPTEN__)
static EM_BOOL on_browser_resize(int eventType, const EmscriptenUiEvent *uiEvent, void *userData)
{
    (void)eventType;
    AppState *state = static_cast<AppState *>(userData);
    if (!state)
        return EM_TRUE;

    int w = uiEvent->windowInnerWidth;
    int h = uiEvent->windowInnerHeight;
    if (w <= 0 || h <= 0)
        return EM_TRUE;

    // 1. Immediately adjust the canvas drawing buffer to match the new window size
    emscripten_set_canvas_element_size("#canvas", w, h);

    // 2. Immediately update glViewport and redraw the frame!
    renderFrame(state);

    return EM_TRUE;
}
#endif

// =============================================================================
// SDL Callbacks
// =============================================================================

SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[])
{
    (void)argc;
    (void)argv;

#if defined(__EMSCRIPTEN__)
    SDL_SetLogOutputFunction(EmscriptenLogCallback, nullptr);
#endif

#if defined(__ANDROID__)
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
#else
    SDL_SetHint(SDL_HINT_MAIN_CALLBACK_RATE, "60");
#endif

    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        SDL_Log("SDL initialization failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    AppState *state = new AppState();
    *appstate = state;

#if defined(__ANDROID__) || defined(__EMSCRIPTEN__)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
#endif

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

#if defined(__ANDROID__)
    SDL_WindowFlags window_flags = SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    state->window = SDL_CreateWindow("SDL3_image WebP GLES3", 0, 0, window_flags);
#else
    SDL_WindowFlags window_flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    state->window = SDL_CreateWindow("SDL3_image WebP Cross-Platform", 1280, 720, window_flags);
#endif

    if (!state->window)
    {
        SDL_Log("Failed to create SDL window: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    state->glContext = SDL_GL_CreateContext(state->window);
    if (!state->glContext)
    {
        SDL_Log("Failed to create OpenGL context: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    SDL_GL_MakeCurrent(state->window, state->glContext);

#if defined(__EMSCRIPTEN__)
    SDL_GL_SetSwapInterval(1);
    // Register direct callback for browser window resize events
    emscripten_set_resize_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, state, EM_FALSE, on_browser_resize);
#else
    SDL_GL_SetSwapInterval(0);
#endif

#if !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress))
    {
        SDL_Log("Failed to initialize GLAD!");
        return SDL_APP_FAILURE;
    }
#endif

    // Shaders & Program
    GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertexShaderSource, NULL);
    glCompileShader(vertexShader);

    GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragmentShaderSource, NULL);
    glCompileShader(fragmentShader);

    state->shaderProgram = glCreateProgram();
    glAttachShader(state->shaderProgram, vertexShader);
    glAttachShader(state->shaderProgram, fragmentShader);
    glLinkProgram(state->shaderProgram);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    // Quad geometry
    float vertices[] = {
        // Positions   // TexCoords
        -0.5f,  0.5f,  0.0f, 0.0f,
        -0.5f, -0.5f,  0.0f, 1.0f,
         0.5f, -0.5f,  1.0f, 1.0f,

        -0.5f,  0.5f,  0.0f, 0.0f,
         0.5f, -0.5f,  1.0f, 1.0f,
         0.5f,  0.5f,  1.0f, 0.0f
    };

    glGenVertexArrays(1, &state->VAO);
    glGenBuffers(1, &state->VBO);

    glBindVertexArray(state->VAO);
    glBindBuffer(GL_ARRAY_BUFFER, state->VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(0);

    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

#if defined(__ANDROID__)
    const char *assetPath = "textures/clear_01.webp";
#else
    const char *assetPath = "assets/textures/clear_01.webp";
#endif

    state->texture = createTexture(assetPath);

#if defined(__EMSCRIPTEN__)
    // Initialize the canvas buffer size to match the current browser window size
    double init_w = 0, init_h = 0;
    emscripten_get_element_css_size("#canvas", &init_w, &init_h);
    if (init_w > 0 && init_h > 0)
    {
        emscripten_set_canvas_element_size("#canvas", static_cast<int>(init_w), static_cast<int>(init_h));
    }
#endif

    SDL_Log("Application initialized successfully");
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event)
{
    (void)appstate;

    if (event->type == SDL_EVENT_QUIT || 
        event->type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
    {
        return SDL_APP_SUCCESS;
    }

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate)
{
    AppState *state = static_cast<AppState *>(appstate);
    renderFrame(state);
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result)
{
    (void)result;

    if (appstate)
    {
        AppState *state = static_cast<AppState *>(appstate);

        if (state->texture)
            glDeleteTextures(1, &state->texture);
        if (state->VAO)
            glDeleteVertexArrays(1, &state->VAO);
        if (state->VBO)
            glDeleteBuffers(1, &state->VBO);
        if (state->shaderProgram)
            glDeleteProgram(state->shaderProgram);

        if (state->glContext)
        {
            SDL_GL_DestroyContext(state->glContext);
        }
        if (state->window)
        {
            SDL_DestroyWindow(state->window);
        }

        delete state;
    }

    SDL_Quit();
}
