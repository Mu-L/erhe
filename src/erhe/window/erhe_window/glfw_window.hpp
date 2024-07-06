#pragma once

#if defined(ERHE_WINDOW_LIBRARY_GLFW)

#include "erhe_window/window_event_handler.hpp"

#include <string>

struct GLFWwindow;
struct GLFWcursor;

namespace erhe::window
{

using Mouse_cursor = signed int;
constexpr Mouse_cursor Mouse_cursor_None       = -1;
constexpr Mouse_cursor Mouse_cursor_Arrow      =  0;
constexpr Mouse_cursor Mouse_cursor_TextInput  =  1;   // When hovering over InputText, etc.
constexpr Mouse_cursor Mouse_cursor_ResizeAll  =  2;   // (Unused by Dear ImGui functions)
constexpr Mouse_cursor Mouse_cursor_ResizeNS   =  3;   // When hovering over an horizontal border
constexpr Mouse_cursor Mouse_cursor_ResizeEW   =  4;   // When hovering over a vertical border or a column
constexpr Mouse_cursor Mouse_cursor_ResizeNESW =  5;   // When hovering over the bottom-left corner of a window
constexpr Mouse_cursor Mouse_cursor_ResizeNWSE =  6;   // When hovering over the bottom-right corner of a window
constexpr Mouse_cursor Mouse_cursor_Hand       =  7;   // (Unused by Dear ImGui functions. Use for e.g. hyperlinks)
constexpr Mouse_cursor Mouse_cursor_NotAllowed =  8;   // When hovering something with disallowed interaction. Usually a crossed circle.
constexpr Mouse_cursor Mouse_cursor_Crosshair  =  9;   // Crosshair cursor
constexpr Mouse_cursor Mouse_cursor_COUNT      = 10;

class Window_configuration
{
public:
    bool             show                    {true};
    bool             fullscreen              {false};
    bool             use_finish              {false};
    bool             framebuffer_transparency{false};
    int              gl_major                {4};
    int              gl_minor                {6};
    int              width                   {1920};
    int              height                  {1080};
    int              msaa_sample_count       {0};
    int              swap_interval           {1};
    float            sleep_time              {0.0f};
    float            wait_time               {0.01f};
    std::string      title                   {};
    Context_window*  share                   {nullptr};
};

class Context_window
{
public:
    explicit Context_window(const Window_configuration& configuration);

    explicit Context_window(Context_window* share);
    virtual ~Context_window() noexcept;

    [[nodiscard]] auto get_width                    () const -> int;
    [[nodiscard]] auto get_height                   () const -> int;
    [[nodiscard]] auto get_root_window_event_handler() -> Root_window_event_handler&;
    [[nodiscard]] auto is_mouse_captured            () const -> bool;
    [[nodiscard]] auto get_glfw_window              () const -> GLFWwindow*;

    auto open                     (const Window_configuration& configuration) -> bool;
    void make_current             () const;
    void clear_current            () const;
    void swap_buffers             () const;
    void break_event_loop         ();
    void enter_event_loop         ();
    void poll_events              ();
    void get_cursor_position      (float& xpos, float& ypos);
    void get_capture_position     (float& xpos, float& ypos);
    void set_visible              (bool visible);
    void set_cursor               (Mouse_cursor cursor);
    void capture_mouse            (bool capture);
    void handle_key_event         (int key, int scancode, int action, int glfw_modifiers);
    void handle_mouse_button_event(int button, int action, int glfw_modifiers);
    void handle_mouse_wheel_event (double x, double y);
    void handle_mouse_move        (double x, double y);

    [[nodiscard]] auto get_modifier_mask () const -> Key_modifier_mask;
    [[nodiscard]] auto get_device_pointer() const -> void*;
    [[nodiscard]] auto get_window_handle () const -> void*;
    [[nodiscard]] auto get_scale_factor  () const -> float;

private:
    void get_extensions();

    Root_window_event_handler m_root_window_event_handler;
    GLFWwindow*               m_glfw_window          {nullptr};
    Mouse_cursor              m_current_mouse_cursor {Mouse_cursor_Arrow};
    bool                      m_is_event_loop_running{false};
    bool                      m_is_mouse_captured    {false};
    bool                      m_is_window_visible    {false};
    GLFWcursor*               m_mouse_cursor         [Mouse_cursor_COUNT];
    Window_configuration      m_configuration;
    double                    m_last_mouse_x         {0.0};
    double                    m_last_mouse_y         {0.0};
    double                    m_mouse_capture_xpos   {0.0};
    double                    m_mouse_capture_ypos   {0.0};
    double                    m_mouse_virtual_xpos   {0.0};
    double                    m_mouse_virtual_ypos   {0.0};
    int                       m_glfw_key_modifiers{0};

    static int s_window_count;
};

}

#endif // defined(ERHE_WINDOW_LIBRARY_GLFW)
