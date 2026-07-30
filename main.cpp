// Dear ImGui: standalone example application for GLFW + OpenGL 3, using programmable pipeline
// (GLFW is a cross-platform general purpose library for handling windows, inputs, OpenGL/Vulkan/Metal graphics context creation, etc.)

// Learn about Dear ImGui:
// - FAQ                  https://dearimgui.com/faq
// - Getting Started      https://dearimgui.com/getting-started
// - Documentation        https://dearimgui.com/docs (same as your local docs/ folder).
// - Introduction, links and more at the top of imgui.cpp
#include "imgui-custom.h"
#include "imgui-knobs.h"
//#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include <stdio.h>
#define GL_SILENCE_DEPRECATION
#if defined(IMGUI_IMPL_OPENGL_ES2)
#include <GLES2/gl2.h>
#endif
#include <GLFW/glfw3.h> // Will drag system OpenGL headers

#include <vector>
#include <string>
#include <iostream>
#include "driver.h"
#include "tray.h"

#include "Raw_Assets.h"

// This example can also compile and run with Emscripten! See 'Makefile.emscripten' for details.
#ifdef __EMSCRIPTEN__
#include "../libs/emscripten/emscripten_mainloop_stub.h"
#endif

static int driver_indicator = 0;
static bool connected = false;
static bool tray_active = false;
static bool force_quit = false;
std::vector<float> levels = {0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f};
// One set of faders serves all three matrix buses; the tabs above the strips
// pick which mix is being edited, and every mix keeps its own levels and pans.
static std::vector <float> bar_value[MIXER_BUSES];
static std::vector <float> pan_value[MIXER_BUSES];
static int current_mix = 0;
// The pinned output pair moves as one while Link is lit. A vector only
// because toggleButton takes a vector<bool> reference.
static std::vector<bool> out_link = {true};
static std::vector <bool> phase_value;
static std::vector <bool> master_bools = {false,false,false,false,false,false};

static void glfw_error_callback(int error, const char* description)
{
	fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

static GLFWwindow* window = nullptr;
static const char* glsl_version = nullptr;
static float main_scale = 1.0f;
static bool want_hide = false;
static double last_poll = 0.0;

static void window_close_callback(GLFWwindow* win)
{
	// only flag it here, the window is torn down from the main loop:
	// destroying a window inside its own callback is not safe
	if (tray_active && !force_quit) {
		glfwSetWindowShouldClose(win, GLFW_FALSE);
		want_hide = true;
	}
}

// Hiding is a full window teardown rather than glfwHideWindow(): on Wayland a
// re-shown window does not reliably get a configured surface back, and the
// first buffer swap then blocks forever waiting for a frame callback.
static void window_open()
{
	if (window)
		return;
#ifdef GLFW_WAYLAND_APP_ID
	glfwWindowHintString(GLFW_WAYLAND_APP_ID, "bid");
#endif
#ifdef GLFW_X11_CLASS_NAME
	glfwWindowHintString(GLFW_X11_CLASS_NAME, "bid");
	glfwWindowHintString(GLFW_X11_INSTANCE_NAME, "bid");
#endif
	window = glfwCreateWindow((int)(1280 * main_scale), (int)(800 * main_scale), "BiD - Open Source Audient mixer for Linux", nullptr, nullptr);
	if (!window)
		return;
	glfwMakeContextCurrent(window);
	glfwSwapInterval(1); // Enable vsync
	ImGui_ImplGlfw_InitForOpenGL(window, true);
	ImGui_ImplOpenGL3_Init(glsl_version);
	glfwSetWindowCloseCallback(window, window_close_callback);
}

static void window_close()
{
	if (!window)
		return;
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	glfwDestroyWindow(window);
	window = nullptr;
}

void TextCentered(const char* text) {
	float avail = ImGui::GetContentRegionAvail().x;
	float width = ImGui::CalcTextSize(text).x;
	if (width < avail)
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - width) * 0.5f);
	ImGui::TextUnformatted(text);
}

// What each output pair listens to, in route_source order: outputs 1+2,
// outputs 3+4, phones. The hardware keeps its routing across power cycles
// and never answers reads, so whatever was last written anywhere stays in
// force until connect pushes this. The first pair carries the main mix, the
// second the alternate speakers, and the phones cue A: the main feed is the
// monitor section's, so dim and cut would land in the headphones too, while
// on a cue the phones answer only to their own dial.
static const int route_default[3] = { ROUTE_MAIN, ROUTE_ALT, ROUTE_CUE_A };
static int route_state[3] = { ROUTE_MAIN, ROUTE_ALT, ROUTE_CUE_A };

static void reset_routing()
{
	for (int p = 0; p < 3; p++)
		route_state[p] = route_default[p];
}

static void reset_mixes()
{
	for (int m = 0; m < MIXER_BUSES; m++) {
		bar_value[m].clear();
		pan_value[m].clear();
	}
}

// Whether this device answers reads on the monitor entity. Probed once on
// connect, so a device that stalls is asked exactly once instead of being
// polled forever.
static bool hw_readback = false;

// Take the front panel's word for the monitor section: those controls exist on
// the device itself and can be changed without the application ever knowing.
static void sync_state_from_device()
{
	for (int i = 0; i < 5; i++) {
		bool on;
		if (get_bool_state(i, &on) && on != (bool)master_bools[i]) {
			master_bools[i] = on;
			masterToggle[i] = on;
			tray_set_master(i, on);
		}
	}
	float v;
	if (get_monitor_volume(&v))
		levels[0] = v;
}

// Nothing in the matrix or the output feature unit can be read back, so on
// connect we send what the window is showing. Without this a control does
// nothing at all until it happens to be nudged, and until then the app and
// the hardware quietly disagree.
static void push_state_to_device()
{
	for (int m = 0; m < MIXER_BUSES; m++)
		for (size_t i = 0; i < bar_value[m].size(); i++)
			set_channel_send(i, m, bar_value[m][i], pan_value[m][i]);
	for (size_t i = 0; i < phase_value.size(); i++)
		set_phase(i, phase_value[i]);
	set_hp_volume(levels[1]);
	for (int p = 0; p < 3; p++)
		set_route_pair(p, route_state[p]);
}

bool toggleButton(std::string name, ImVec2 size, std::vector<bool>::reference value) {
	int mastercol = 0;
	bool state = false;
	if (value){
		ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered)); mastercol++;
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.1,0.1,0.1,1.0)); mastercol++;
	}
	if (ImGui::Button(name.c_str(), size)) {state = true; value.flip();};
	
	if (mastercol > 0)
		ImGui::PopStyleColor(mastercol);

	return state;
}

// Main code
int main(int, char**)
{
	glfwSetErrorCallback(glfw_error_callback);
	if (!glfwInit())
		return 1;

	// Decide GL+GLSL versions
#if defined(IMGUI_IMPL_OPENGL_ES2)
	// GL ES 2.0 + GLSL 100 (WebGL 1.0)
	glsl_version ="#version 100";
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
	glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
#elif defined(IMGUI_IMPL_OPENGL_ES3)
	// GL ES 3.0 + GLSL 300 es (WebGL 2.0)
	glsl_version ="#version 300 es";
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
	glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
#elif defined(__APPLE__)
	// GL 3.2 + GLSL 150
	glsl_version ="#version 150";
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);  // 3.2+ only
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);            // Required on Mac
#else
	// GL 3.0 + GLSL 130
	glsl_version ="#version 130";
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
	//glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);  // 3.2+ only
	//glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);            // 3.0+ only
#endif

	main_scale = ImGui_ImplGlfw_GetContentScaleForMonitor(glfwGetPrimaryMonitor()); // Valid on GLFW 3.3+ only

	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

	// Setup Dear ImGui style
	//ImGui::StyleColorsLight();

	// Setup scaling
	ImGuiStyle& style = ImGui::GetStyle();
	ImGui::StyleColorsBiD(&style);
	style.ScaleAllSizes(main_scale);        // Bake a fixed style scale. (until we have a solution for dynamic style scaling, changing this requires resetting Style + calling this again)
	style.FontScaleDpi = main_scale;        // Set initial font scale. (using io.ConfigDpiScaleFonts=true makes this unnecessary. We leave both here for documentation purpose)

	// Setup Platform/Renderer backends (window and backends are torn down and
	// rebuilt together whenever BiD hides to the tray)
	window_open();
	if (window == nullptr)
		return 1;
#ifdef __EMSCRIPTEN__
	ImGui_ImplGlfw_InstallEmscriptenCallbacks(window, "#canvas");
#endif

	// Load Fonts
	style.FontSizeBase = 20.0f;
    ImFontConfig fc;
    fc.FontDataOwnedByAtlas = false;
    fc.OversampleH = 4;
    fc.OversampleV = 4;
    fc.PixelSnapH = true;
    fc.MergeMode = false;

	ImFont* font = io.Fonts->AddFontFromMemoryTTF((void*)GORDITA_REGULAR_OTF, GORDITA_REGULAR_OTF_SIZE, 20, &fc);
	ImFont* audiofont = io.Fonts->AddFontFromMemoryTTF((void*)FONTAUDIO_TTF, FONTAUDIO_TTF_SIZE, 20, &fc);
	io.Fonts->Build();

	// Our state
	bool show_routing = false;
	bool show_another_window = false;
	ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

	//Tray icon: while one is available, closing the window hides BiD
	//instead of quitting (quit through Menu->Quit or the tray-less close)
	tray_active = tray_init() != 0;

	//Init all the device properties
	setup_devices();
	//Probe for known usb devices
	int _dev = device_probe();
	if (_dev >= 0) {
		driver_indicator = _dev;
	    reset_mixes();
	    phase_value.clear();
	    reset_routing();
	}

	// Main loop
#ifdef __EMSCRIPTEN__
	// For an Emscripten build we are disabling file-system access, so let's not attempt to do a fopen() of the imgui.ini file.
	// You may manually call LoadIniSettingsFromMemory() to load settings from your own storage.
	io.IniFilename = nullptr;
	EMSCRIPTEN_MAINLOOP_BEGIN
#else
	while (!force_quit && (window == nullptr || !glfwWindowShouldClose(window)))
#endif
	{
		glfwPollEvents();
		int tray_action = tray_pump();
		if (tray_action == TRAY_TOGGLE) {
			if (window)
				want_hide = true;
			else
				window_open();
		}
		else if (tray_action == TRAY_QUIT) {
			force_quit = true;
		}
		else if (tray_action >= TRAY_MASTER) {
			int idx = tray_action - TRAY_MASTER;
			master_bools[idx] = !master_bools[idx];
			if (connected)
				set_bool_state(idx);
			tray_set_master(idx, master_bools[idx]);
		}
		// The monitor level is the one control that gets watched while it
		// moves, so it is read every tick. The toggles are discrete and go
		// round robin, one per tick, which still catches them in a quarter
		// of a second. Two transfers a tick keeps this far away from the
		// rate that saturates the protocol.
		if (connected && hw_readback && glfwGetTime() - last_poll > 0.05 && !ImGui::IsAnyItemActive()) {
			last_poll = glfwGetTime();
			float v;
			if (get_monitor_volume(&v) && (v > levels[0] + 0.002f || v < levels[0] - 0.002f))
				levels[0] = v;
			static int poll_idx = 0;
			bool on;
			if (get_bool_state(poll_idx, &on) && on != (bool)master_bools[poll_idx]) {
				master_bools[poll_idx] = on;
				masterToggle[poll_idx] = on;
				tray_set_master(poll_idx, on);
			}
			poll_idx = (poll_idx + 1) % 5;
		}
		if (want_hide) {
			want_hide = false;
			window_close();
		}
		if (window == nullptr)
		{
			// hidden in the tray: no rendering, just keep the tray responsive
			ImGui_ImplGlfw_Sleep(100);
			continue;
		}
		if (glfwGetWindowAttrib(window, GLFW_ICONIFIED) != 0)
		{
			ImGui_ImplGlfw_Sleep(10);
			continue;
		}

		// Start the Dear ImGui frame
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();
		int stylecount = 0;
		ImGui::PushStyleVar(ImGuiStyleVar_SeparatorTextAlign, ImVec2(0.5f,0.5f)); stylecount++;
		ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f); stylecount++;
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f); stylecount++;
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f); stylecount++;

		{
			// Everything is laid out against the viewport rather than the size
			// the window happened to start at, so it reflows when resized.
			const ImGuiViewport* viewport = ImGui::GetMainViewport();
			const float absX = viewport->Size.x;
			const float absY = viewport->Size.y;
			// the monitor panel stays usable instead of scaling without bound
			const float panel_w = ImClamp(absX * 0.2f, 240.0f * main_scale, 420.0f * main_scale);
			const float mixer_w = absX - panel_w;

			ImGui::SetNextWindowPos(viewport->Pos);
			ImGui::SetNextWindowSize(ImVec2(mixer_w, absY));
			ImGui::Begin("BiD - Open Source Audient mixer for Linux", nullptr, ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBringToFrontOnFocus);
			if (ImGui::BeginMenuBar())
			{
				if (ImGui::BeginMenu("Menu"))
				{
					if (ImGui::MenuItem("Driver Select")) {show_another_window = !show_another_window;};
					if (ImGui::MenuItem("Routing")) {show_routing = !show_routing;};
					ImGui::Separator();
					if (ImGui::MenuItem("Quit")) {force_quit = true; glfwSetWindowShouldClose(window, GLFW_TRUE);};
					ImGui::EndMenu();
				}

				ImGui::EndMenuBar();
			}
			//TODO: Remove temp
			//TEMP "OFFLINE" UI
			bool test = true;

			if (connected || test) { //Main controls
				if (bar_value[0].size() == 0) {
					const device_properties &dev_init = devices[driver_indicator];
					for (size_t i = 0; i < devices[driver_indicator].mic_inputs+devices[driver_indicator].digital_inputs; i++) {
						bool is_monitor = dev_init.monitor_pair >= 0
							&& ((int)i - dev_init.mic_inputs == dev_init.monitor_pair
							 || (int)i - dev_init.mic_inputs == dev_init.monitor_pair + 1);
						phase_value.push_back(false);
						// Everything starts centred except the digital pair
						// that feeds the monitor outputs, which is the one
						// place we know is stereo: panning it apart is what
						// keeps its image instead of summing it to the middle.
						int d = (int)i - dev_init.mic_inputs;
						float pan = 0.5f;
						if (dev_init.monitor_pair >= 0 && d == dev_init.monitor_pair)
							pan = 0.0f;
						else if (dev_init.monitor_pair >= 0 && d == dev_init.monitor_pair + 1)
							pan = 1.0f;
						// Every mix starts the same: only the monitor pair
						// open, so an output switched onto a cue hears the
						// computer right away rather than silence.
						for (int m = 0; m < MIXER_BUSES; m++) {
							bar_value[m].push_back(is_monitor ? 1.0f : 0.0f);
							pan_value[m].push_back(pan);
						}
					}
				}
				// Which mix the faders edit. The other two routing sources
				// have nothing to mix: Alt is the main mix on the other
				// speakers, DAW Thru bypasses the mixer at a fixed level.
				if (ImGui::BeginTabBar("mixtabs")) {
					const char* mix_names[MIXER_BUSES] = { "Main Mix", "Cue A", "Cue B" };
					for (int m = 0; m < MIXER_BUSES; m++)
						if (ImGui::BeginTabItem(mix_names[m])) { current_mix = m; ImGui::EndTabItem(); }
					if (ImGui::TabItemButton("?", ImGuiTabItemFlags_Trailing | ImGuiTabItemFlags_NoTooltip)) {}
					if (ImGui::IsItemHovered())
						ImGui::SetTooltip("These are the three mixes the hardware can build.\n"
							"Alt Spkr has no page: it is the Main Mix on the other speakers.\n"
							"DAW Thru has no page: it bypasses the mixer at full level.");
					ImGui::EndTabBar();
				}
				// keep one line free underneath for the version string
				const float strip_h = ImGui::GetContentRegionAvail().y - ImGui::GetTextLineHeightWithSpacing();
				// a channel is: label, spacer, fader, spacer, phase button.
				// The fader absorbs whatever height the rest leaves over.
				const float fader_w = 42.0f * main_scale;
				// Columns share the width evenly. A column can never be
				// narrower than its label, or the labels would push the row
				// wider than the child and force a scrollbar.
				const int chan_count = devices[driver_indicator].mic_inputs + devices[driver_indicator].digital_inputs;
				const float label_w = ImGui::CalcTextSize((std::string("Digi ")
						+ std::to_string(devices[driver_indicator].digital_inputs)).c_str()).x;
				float chan_w = ImMax(fader_w, label_w);
				if (chan_count > 0)
					chan_w = ImClamp((ImGui::GetContentRegionAvail().x - style.ItemSpacing.x * (chan_count - 1)) / chan_count,
						ImMax(fader_w, label_w), ImMax(96.0f * main_scale, label_w));
				const float pad_top = 24.0f * main_scale;
				const float pad_bottom = 32.0f * main_scale;
				const float phase_h = 40.0f * main_scale;
				const float pan_h = ImGui::GetFrameHeight();
				// Both strip children leave room for the scrollbar only the
				// scrolling one shows, so the pinned faders stay level with
				// the rest.
				const float fader_h = ImMax(strip_h
						- (ImGui::GetTextLineHeightWithSpacing() + pad_top + pad_bottom + phase_h + pan_h
						   + style.ItemSpacing.y * 5.0f + style.ScrollbarSize),
					80.0f * main_scale);
				// centre an item of the given width inside the current column
				auto center_in_column = [&](float item_w) { ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (chan_w - item_w) * 0.5f); };
				// Pan is the ratio between a channel's two sends on the mix
				// being edited, which is exactly how the matrix places it.
				auto pan_slider = [&](int idx, const std::string& id) {
					char fmt[16];
					float p = pan_value[current_mix][idx];
					if (p < 0.499f)      snprintf(fmt, sizeof(fmt), "L%.0f", (0.5f - p) * 200.0f);
					else if (p > 0.501f) snprintf(fmt, sizeof(fmt), "R%.0f", (p - 0.5f) * 200.0f);
					else                 snprintf(fmt, sizeof(fmt), "C");
					center_in_column(fader_w);
					ImGui::SetNextItemWidth(fader_w);
					if (ImGui::SliderFloat(("##pan" + id).c_str(), &pan_value[current_mix][idx], 0.0f, 1.0f, fmt)) {
						if (connected)
							set_channel_send(idx, current_mix, bar_value[current_mix][idx], pan_value[current_mix][idx]);
					}
				};
				// one whole strip: label, fader, pan, phase. idx is the
				// matrix input the strip drives, wid keeps widget ids apart.
				// A partner channel, when given, follows the fader so a
				// linked stereo pair moves as one.
				float link_row_y = 0.0f;
				auto draw_strip = [&](int idx, const std::string& label, const std::string& wid, bool first, int partner = -1) {
					ImGui::BeginGroup();
					if (first)
						ImGui::SetCursorPosY(3 * main_scale);
					center_in_column(ImGui::CalcTextSize(label.c_str()).x); ImGui::TextUnformatted(label.c_str());
					ImGui::Dummy(ImVec2(chan_w,pad_top));
					center_in_column(fader_w); if (ImGui::VFaderFloat(("##v"+wid).c_str(), ImVec2(fader_w, fader_h), &bar_value[current_mix][idx], 0.0f, 1.0f, "%.2f")) {
						if (partner >= 0)
							bar_value[current_mix][partner] = bar_value[current_mix][idx];
						if (connected) {
							set_channel_send(idx, current_mix, bar_value[current_mix][idx], pan_value[current_mix][idx]);
							if (partner >= 0)
								set_channel_send(partner, current_mix, bar_value[current_mix][partner], pan_value[current_mix][partner]);
						}
					};
					pan_slider(idx, wid);
					link_row_y = ImGui::GetCursorPosY();
					ImGui::Dummy(ImVec2(0,pad_bottom)); center_in_column(fader_w);
					ImGui::PushFont(audiofont, 32);
					if (toggleButton("###Phase"+wid, ImVec2(fader_w, phase_h), phase_value[idx])) {if (connected) set_phase_state(idx);};
					ImGui::PopFont();
					ImGui::EndGroup();
				};

				const device_properties &dev = devices[driver_indicator];
				// The DAW return pair that feeds outputs 1 and 2 lives at the
				// far end of the digital inputs, but on screen it belongs
				// first: it gets its own child on the left, pinned, so it
				// stays put while the input strips scroll past next to it.
				const int mon_idx = dev.monitor_pair >= 0 ? dev.mic_inputs + dev.monitor_pair : -1;
				if (mon_idx >= 0) {
					ImGui::BeginChild("PinnedOuts", ImVec2(chan_w * 2.0f + style.ItemSpacing.x, strip_h));
					draw_strip(mon_idx,     "L Out", "OutL", true,  out_link[0] ? mon_idx + 1 : -1);
					ImGui::SameLine();
					draw_strip(mon_idx + 1, "R Out", "OutR", false, out_link[0] ? mon_idx : -1);
					// the resting band under the pan sliders carries the link
					// toggle: lit, the two faders move as one
					ImGui::SetCursorPos(ImVec2(0, link_row_y + style.ItemSpacing.y * 0.5f));
					if (toggleButton("Link", ImVec2(chan_w * 2.0f + style.ItemSpacing.x, pad_bottom - style.ItemSpacing.y), out_link[0])) {
						if (out_link[0]) {
							// relinking snaps the right side back onto the left
							bar_value[current_mix][mon_idx + 1] = bar_value[current_mix][mon_idx];
							if (connected)
								set_channel_send(mon_idx + 1, current_mix, bar_value[current_mix][mon_idx + 1], pan_value[current_mix][mon_idx + 1]);
						}
					}
					ImGui::EndChild();
					ImGui::SameLine();
				}
				ImGui::BeginChild("Faders", ImVec2(0, strip_h), 0, ImGuiWindowFlags_HorizontalScrollbar);
				bool first = true;
				for (size_t i = 0; i < (devices[driver_indicator].mic_inputs); i++) {
					if (!first) ImGui::SameLine();
					draw_strip(i, std::string("Mic ")+std::to_string(i+1), "Mic"+std::to_string(i), first);
					first = false;
				}
				for (size_t i = 0; i < (devices[driver_indicator].digital_inputs); i++) {
					if (dev.monitor_pair >= 0 && ((int)i == dev.monitor_pair || (int)i == dev.monitor_pair + 1))
						continue; // pinned on the left as L Out / R Out
					if (!first) ImGui::SameLine();
					draw_strip(dev.mic_inputs + i, std::string("Digi ")+std::to_string(i+1), "Digi"+std::to_string(i), first);
					first = false;
				}

				ImGui::EndChild();
			}
			ImGui::Text(VERSION_BID);
			//ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
			ImGui::End();

			ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + mixer_w, viewport->Pos.y));
			ImGui::SetNextWindowSize(ImVec2(panel_w, absY));
			ImGui::Begin("Monitor", nullptr, ImGuiWindowFlags_NoDecoration);
			ImGui::SeparatorText("Connection");
			
			TextCentered((std::string("Selected Driver: ") + devices[driver_indicator].name).c_str());
			if (connected) {
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0,1.0,0.0,0.8));
				TextCentered("Connected");
			}
			else {
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0,0.0,0.0,0.8));
				TextCentered("Disconnected");
			} ImGui::PopStyleColor();

			std::string name = "Connect";
			if (connected)
				name = "Disconnect";
			
			if (ImGui::Button(name.c_str(),ImVec2(ImGui::GetContentRegionAvail().x, 40 * main_scale))) {
				connected = !connected;
				if (connected) {
					if (!driver_init(devices[driver_indicator].usb_id)) {
						connected = false;
						ImGui::OpenPopup("No connection possible");
					}
					else {
						float probe;
						hw_readback = get_monitor_volume(&probe) != 0;
						if (hw_readback)
							sync_state_from_device();
						push_state_to_device();
					}
				}
				else
					driver_shutdown();
			};
		   // Always center this window when appearing
			ImVec2 center = ImGui::GetMainViewport()->GetCenter();
			ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

			if (ImGui::BeginPopupModal("No connection possible", NULL, ImGuiWindowFlags_AlwaysAutoResize))
			{
				ImGui::Text("USB Device can not be opened.");
				ImGui::Text("Make sure you have selected the correct driver and your usb permissions are correct.");
				ImGui::Text("This can either be done by adding the usb device to the udev rules,");
				ImGui::Text("or running BiD with sudo permissions.");
				ImGui::Dummy(ImVec2(10,20 * main_scale));
				ImGui::Separator();
				if (ImGui::Button("OK", ImVec2(120 * main_scale, 0))) { ImGui::CloseCurrentPopup(); }
				ImGui::SetItemDefaultFocus();
				ImGui::EndPopup();
			}

			ImGui::SeparatorText("Monitor");

			// the knobs sit centred in whatever width the panel has, and the
			// toggles are anchored to the bottom of it
			const float knob_w = ImGui::GetTextLineHeight() * 4.0f;
			const float btn_h = 40.0f * main_scale;
			const float toggles_h = btn_h * 2.0f + style.ItemSpacing.y;
			const float knob_gap = ImMax((absY - ImGui::GetCursorPosY() - toggles_h
					- style.WindowPadding.y - knob_w * 2.0f - ImGui::GetTextLineHeightWithSpacing() * 4.0f) / 3.0f,
				style.ItemSpacing.y);

			ImGui::Dummy(ImVec2(0,knob_gap));
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (ImGui::GetContentRegionAvail().x - knob_w) * 0.5f);
			if (ImGuiKnobs::Knob("Main LR", &levels[0], 0.0f, 1.0f, 0.01f, "%.2f", ImGuiKnobVariant_Wiper)) {if (connected) set_speaker_volume(levels[0]);}
			ImGui::Dummy(ImVec2(0,knob_gap));
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (ImGui::GetContentRegionAvail().x - knob_w) * 0.5f);
			if (ImGuiKnobs::Knob("Phones", &levels[1], 0.0f, 1.0f, 0.01f, "%.2f", ImGuiKnobVariant_Wiper)) {if (connected) set_hp_volume(levels[1]);}

			ImGui::SetCursorPosY(ImMax(ImGui::GetCursorPosY(), absY - toggles_h - style.WindowPadding.y));
			const float btn_w = (ImGui::GetContentRegionAvail().x - style.ItemSpacing.x * 2.0f) / 3.0f;
			ImGui::BeginGroup();
			if (toggleButton("Dim", ImVec2(btn_w, btn_h), master_bools[0])) { if (connected) {set_bool_state(0);} tray_set_master(0, master_bools[0]);};
			ImGui::SameLine();
			if (toggleButton("Alt", ImVec2(btn_w, btn_h), master_bools[1])) { if (connected) {set_bool_state(1);} tray_set_master(1, master_bools[1]);};
			ImGui::SameLine();
			if (toggleButton("Talk", ImVec2(btn_w, btn_h), master_bools[2])) { if (connected) {set_bool_state(2);} tray_set_master(2, master_bools[2]);};
			ImGui::BeginGroup();
			if (toggleButton("Phase", ImVec2(btn_w, btn_h), master_bools[3])) { if (connected) {set_bool_state(3);} tray_set_master(3, master_bools[3]);};
			ImGui::EndGroup();
			ImGui::SameLine();
			if (toggleButton("Mono", ImVec2(btn_w, btn_h), master_bools[4])) { if (connected) {set_bool_state(4);} tray_set_master(4, master_bools[4]);};
			ImGui::EndGroup();


			ImGui::End();
		}

		// 3. Show another simple window.
		if (show_another_window)
		{
			// A floor on the size, so the window is usable even when imgui.ini
			// carries a size saved at a different display scale.
			ImGui::SetNextWindowSizeConstraints(ImVec2(380 * main_scale, 130 * main_scale), ImVec2(FLT_MAX, FLT_MAX));
			ImGui::SetNextWindowSize(ImVec2(420 * main_scale, 140 * main_scale), ImGuiCond_FirstUseEver);
			ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
			ImGui::Begin("Driver Select", &show_another_window);   // Pass a pointer to our bool variable (the window will have a closing button that will clear the bool when clicked)
	        
	        //const char* combo_preview_value = items[item_selected_idx];
	        if (ImGui::BeginCombo("Interface type", devices[driver_indicator].name.c_str()))
	        {
	            for (int n = 0; n < devices.size(); n++)
	            {
	                const bool is_selected = (driver_indicator == n);
	                if (ImGui::Selectable(devices[n].name.c_str(), is_selected)) {
	                    driver_indicator = n;
	                    reset_mixes();
	                    phase_value.clear();
	                    reset_routing();
	                }

	                // Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
	                if (is_selected)
	                    ImGui::SetItemDefaultFocus();
	            }
	            ImGui::EndCombo();
	        }

			//ImGui::Combo("Interface type", &driver_indicator, items, IM_ARRAYSIZE(items));
			ImGui::Text("USB ID: 0x%04X", devices[driver_indicator].usb_id);
			ImGui::End();
		}
		if (show_routing)
		{
			ImGui::SetNextWindowSizeConstraints(ImVec2(460 * main_scale, 250 * main_scale), ImVec2(FLT_MAX, FLT_MAX));
			ImGui::SetNextWindowSize(ImVec2(500 * main_scale, 310 * main_scale), ImGuiCond_FirstUseEver);
			ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
			ImGui::Begin("Routing", &show_routing);
			// One source per physical output pair, matching how the official
			// app treats routing. The rows are the routing outputs in device
			// order: 0/1, 2/3, then the phones on 4/5.
			const char* pair_names[]   = { "Out 1+2", "Out 3+4", "Phones" };
			const char* source_names[] = { "Main Mix", "Alt Spkr", "Cue A", "Cue B", "DAW Thru" };
			ImGui::TextWrapped(
				"Each output pair plays one source. Main Mix is the monitor section's feed: the "
				"volume knob, Dim and Cut apply on any output carrying it, phones included. "
				"Alt Spkr is that same feed for a second set of speakers, switched in with Alt. "
				"The cues are separate mixes, clear of the monitor section - the phones sit on "
				"Cue A so the speaker buttons leave them alone and the Phones dial sets their "
				"level. DAW Thru comes straight from the computer at full level - no control at all.");
			ImGui::Spacing();

			if (ImGui::BeginTable("routing", 1 + ROUTE_SOURCES, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInnerH))
			{
				ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed,
					ImGui::CalcTextSize("Out 0+0").x + style.CellPadding.x * 2.0f);
				for (int s = 0; s < ROUTE_SOURCES; s++)
					ImGui::TableSetupColumn(source_names[s], ImGuiTableColumnFlags_WidthFixed,
						ImGui::CalcTextSize(source_names[s]).x + style.CellPadding.x * 2.0f);
				ImGui::TableHeadersRow();
				for (int pair = 0; pair < 3; pair++)
				{
					ImGui::PushID(pair);
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::AlignTextToFramePadding();
					ImGui::TextUnformatted(pair_names[pair]);
					for (int s = 0; s < ROUTE_SOURCES; s++)
						if (ImGui::TableSetColumnIndex(s + 1))
						{
							ImGui::PushID(s);
							// centre the radio under its header
							float avail = ImGui::GetContentRegionAvail().x;
							float rw = ImGui::GetFrameHeight();
							if (avail > rw)
								ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - rw) * 0.5f);
							if (ImGui::RadioButton("", &route_state[pair], s)) {
								if (connected)
									set_route_pair(pair, s);
							};
							ImGui::PopID();
						}
					ImGui::PopID();
				}
				ImGui::EndTable();
			}
			ImGui::Spacing();
			if (ImGui::Button("Reset to defaults")) {
				reset_routing();
				if (connected)
					for (int p = 0; p < 3; p++)
						set_route_pair(p, route_state[p]);
			}
			ImGui::End();
		}
		ImGui::PopStyleVar(stylecount);
		// Rendering
		ImGui::Render();
		int display_w, display_h;
		glfwGetFramebufferSize(window, &display_w, &display_h);
		glViewport(0, 0, display_w, display_h);
		glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w);
		glClear(GL_COLOR_BUFFER_BIT);
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

		glfwSwapBuffers(window);
	}
#ifdef __EMSCRIPTEN__
	EMSCRIPTEN_MAINLOOP_END;
#endif

	tray_shutdown();
	driver_shutdown(); //just in case disconnect
	// Cleanup (window_close() is a no-op when already hidden to the tray)
	window_close();
	ImGui::DestroyContext();

	glfwTerminate();

	return 0;
}
