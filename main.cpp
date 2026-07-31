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
#include <cstring>
#include <cstdlib>
#include <csignal>
#include <cmath>
#include <algorithm>
#include <sys/stat.h>
#include <dirent.h>
#include <cctype>
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
// The input strips link in twos as well - MIC 1+2, and the digital inputs
// pair by pair - keyed by the pair's left channel. They start unlinked:
// two mics are usually two sources, not one stereo one.
static std::vector<bool> chan_link;
// And any pair - inputs or the pinned outputs - can be summed to mono:
// while lit, both sides hear both channels. Keyed like chan_link.
static std::vector<bool> chan_mono;
// A channel can be named for what it carries; empty keeps the stock
// label. rename_idx is the strip being edited right now, if any.
static std::vector<std::string> chan_name;
static int rename_idx = -1;
static char rename_buf[24];
static bool rename_focus = false;
// One-shot: the mix tab the state file wants selected on the first frame.
static int want_mix_tab = -1;
// Per mix: a master trim over everything it sends, and mute/solo per
// channel. None of this exists in hardware - it is baked into the levels
// that reach the matrix. Solo mutes everyone who is not soloed.
static float mix_master[MIXER_BUSES] = {1.0f, 1.0f, 1.0f};
static std::vector<bool> mute_value[MIXER_BUSES];
static std::vector<bool> solo_value[MIXER_BUSES];
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

// A delayed tooltip on whatever was drawn last: for buttons whose label
// is an abbreviation rather than a name.
static void hover_tip(const char* text)
{
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
		ImGui::SetTooltip("%s", text);
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
// The fourth entry is the loopback pair: not a jack, but routing outputs
// 10 and 11, whose signal the hardware hands back to the computer as
// capture channels 11+12 (decoded by Monix from the official app). Its
// default is DAW Thru: silent unless something plays into 11+12, and
// never coupled to the monitor knob by surprise.
static const int route_default[4] = { ROUTE_MAIN, ROUTE_ALT, ROUTE_CUE_A, ROUTE_DAW };
static int route_state[4] = { ROUTE_MAIN, ROUTE_ALT, ROUTE_CUE_A, ROUTE_DAW };

static void reset_routing()
{
	for (int p = 0; p < 4; p++)
		route_state[p] = route_default[p];
}

static void reset_mixes()
{
	for (int m = 0; m < MIXER_BUSES; m++) {
		bar_value[m].clear();
		pan_value[m].clear();
		mute_value[m].clear();
		solo_value[m].clear();
		mix_master[m] = 1.0f;
	}
	chan_link.clear();
	chan_mono.clear();
	chan_name.clear();
}

// Which pair does a channel belong to? Returns the pair's left channel,
// or -1 for a channel with no partner. The digital inputs pair on even
// boundaries, the same way the strips are drawn; the monitor pair is a
// pair like any other.
static int pair_left_of(int idx)
{
	const device_properties &dev = devices[driver_indicator];
	if (idx < dev.mic_inputs)
		return ((idx & ~1) + 1 < dev.mic_inputs) ? (idx & ~1) : -1;
	int d = idx - dev.mic_inputs;
	int base = d & ~1;
	if (base + 1 >= dev.digital_inputs)
		return -1;
	return dev.mic_inputs + base;
}

// What actually reaches the matrix: the fader times the mix master, and
// nothing at all when the channel is muted or someone else is soloed.
static void send_channel(int idx, int m)
{
	bool any_solo = false;
	for (size_t i = 0; i < solo_value[m].size(); i++)
		if (solo_value[m][i]) { any_solo = true; break; }
	float v = bar_value[m][idx] * mix_master[m];
	if (idx < (int)mute_value[m].size()
	    && (mute_value[m][idx] || (any_solo && !solo_value[m][idx])))
		v = 0.0f;
	float p = pan_value[m][idx];
	int pl = pair_left_of(idx);
	if (pl >= 0 && pl < (int)chan_mono.size() && chan_mono[pl])
		p = 0.5f; // mono pair: centre both, so both sides hear both
	set_channel_send(idx, m, v, p);
}

static void send_mix(int m)
{
	for (size_t i = 0; i < bar_value[m].size(); i++)
		send_channel(i, m);
}

// A restart should come back with the mixes, routing and levels it left
// with. Nothing of that can be read out of the hardware, so a plain text
// file per device, keyed by USB id, is the only memory there is. It lives
// in $XDG_CONFIG_HOME/bid, or ~/.config/bid.
static std::string state_path()
{
	if (devices.empty())
		return "";
	std::string base;
	const char *xdg = getenv("XDG_CONFIG_HOME");
	if (xdg && *xdg) {
		base = xdg;
	} else {
		const char *home = getenv("HOME");
		if (!home || !*home)
			return "";
		base = std::string(home) + "/.config";
	}
	char name[40];
	snprintf(name, sizeof(name), "/bid/state-%04x.conf", devices[driver_indicator].usb_id);
	return base + name;
}

static void save_state_to(const std::string& path)
{
	if (path.empty() || bar_value[0].empty())
		return;
	std::string dir = path.substr(0, path.rfind('/'));
	mkdir(dir.substr(0, dir.rfind('/')).c_str(), 0755);
	mkdir(dir.c_str(), 0755);
	std::string tmp = path + ".tmp";
	FILE *f = fopen(tmp.c_str(), "w");
	if (!f)
		return;
	size_t n = bar_value[0].size();
	fprintf(f, "bid-state 1\nchannels %zu\n", n);
	for (int m = 0; m < MIXER_BUSES; m++) {
		fprintf(f, "levels %d", m);
		for (size_t i = 0; i < n; i++)
			fprintf(f, " %.6f", bar_value[m][i]);
		fprintf(f, "\npans %d", m);
		for (size_t i = 0; i < n; i++)
			fprintf(f, " %.6f", pan_value[m][i]);
		fprintf(f, "\n");
	}
	fprintf(f, "phase");
	for (size_t i = 0; i < n && i < phase_value.size(); i++)
		fprintf(f, " %d", phase_value[i] ? 1 : 0);
	fprintf(f, "\nroute %d %d %d\n", route_state[0], route_state[1], route_state[2]);
	fprintf(f, "link %d\n", out_link[0] ? 1 : 0);
	fprintf(f, "phones %.6f\n", levels[1]);
	fprintf(f, "monitor %.6f\n", levels[0]);
	fprintf(f, "tab %d\n", current_mix);
	fprintf(f, "masters %.6f %.6f %.6f\n", mix_master[0], mix_master[1], mix_master[2]);
	for (int m = 0; m < MIXER_BUSES; m++) {
		fprintf(f, "mutes %d", m);
		for (size_t i = 0; i < n && i < mute_value[m].size(); i++)
			fprintf(f, " %d", mute_value[m][i] ? 1 : 0);
		fprintf(f, "\nsolos %d", m);
		for (size_t i = 0; i < n && i < solo_value[m].size(); i++)
			fprintf(f, " %d", solo_value[m][i] ? 1 : 0);
		fprintf(f, "\n");
	}
	fprintf(f, "pairlinks");
	for (size_t i = 0; i < n && i < chan_link.size(); i++)
		fprintf(f, " %d", chan_link[i] ? 1 : 0);
	fprintf(f, "\npairmono");
	for (size_t i = 0; i < n && i < chan_mono.size(); i++)
		fprintf(f, " %d", chan_mono[i] ? 1 : 0);
	fprintf(f, "\nloopback %d\n", route_state[3]);
	for (size_t i = 0; i < n && i < chan_name.size(); i++)
		if (!chan_name[i].empty())
			fprintf(f, "name %zu %s\n", i, chan_name[i].c_str());
	fclose(f);
	// written to the side and renamed over, so a crash mid-write cannot
	// leave a half file where the good one was
	rename(tmp.c_str(), path.c_str());
}

static void save_state() { save_state_to(state_path()); }

// All or nothing: a file that does not parse, or that was written for a
// different channel count, is ignored and the defaults stand.
static bool load_state_from(const std::string& path)
{
	if (path.empty())
		return false;
	FILE *f = fopen(path.c_str(), "r");
	if (!f)
		return false;
	const device_properties &dev = devices[driver_indicator];
	const long want = dev.mic_inputs + dev.digital_inputs;
	char key[16] = {0};
	int ver = 0;
	long n = 0;
	std::vector<float> lv[MIXER_BUSES], pv[MIXER_BUSES];
	std::vector<char> ph;
	int route[3] = {0}, link = 1, tab = 0;
	float phones = 0.0f, monitor = 0.0f;
	float mm[MIXER_BUSES] = {1.0f, 1.0f, 1.0f};
	std::vector<char> mu[MIXER_BUSES], so[MIXER_BUSES];
	auto clamp01 = [](float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); };
	bool ok = fscanf(f, "%15s %d", key, &ver) == 2 && !strcmp(key, "bid-state") && ver == 1
	       && fscanf(f, "%15s %ld", key, &n) == 2 && !strcmp(key, "channels") && n == want;
	auto row = [&](const char *name, std::vector<float> &out) {
		int m = 0;
		if (!ok || fscanf(f, "%15s %d", key, &m) != 2 || strcmp(key, name) != 0) {
			ok = false;
			return;
		}
		for (long i = 0; i < n; i++) {
			float v = 0.0f;
			if (fscanf(f, "%f", &v) != 1) {
				ok = false;
				return;
			}
			out.push_back(clamp01(v));
		}
	};
	for (int m = 0; m < MIXER_BUSES; m++) {
		row("levels", lv[m]);
		row("pans", pv[m]);
	}
	if (ok && (fscanf(f, "%15s", key) != 1 || strcmp(key, "phase") != 0))
		ok = false;
	for (long i = 0; ok && i < n; i++) {
		int v = 0;
		if (fscanf(f, "%d", &v) != 1)
			ok = false;
		else
			ph.push_back(v != 0);
	}
	if (ok && (fscanf(f, "%15s %d %d %d", key, &route[0], &route[1], &route[2]) != 4 || strcmp(key, "route") != 0))
		ok = false;
	if (ok && (fscanf(f, "%15s %d", key, &link) != 2 || strcmp(key, "link") != 0))
		ok = false;
	if (ok && (fscanf(f, "%15s %f", key, &phones) != 2 || strcmp(key, "phones") != 0))
		ok = false;
	if (ok && (fscanf(f, "%15s %f", key, &monitor) != 2 || strcmp(key, "monitor") != 0))
		ok = false;
	if (ok && (fscanf(f, "%15s %d", key, &tab) != 2 || strcmp(key, "tab") != 0))
		ok = false;
	// newer fields: absent from older files, which stay valid without them
	bool extra = ok && fscanf(f, "%15s %f %f %f", key, &mm[0], &mm[1], &mm[2]) == 4 && strcmp(key, "masters") == 0;
	for (int m = 0; extra && m < MIXER_BUSES; m++) {
		int mi = 0;
		extra = fscanf(f, "%15s %d", key, &mi) == 2 && strcmp(key, "mutes") == 0;
		for (long i = 0; extra && i < n; i++) {
			int v = 0;
			if (fscanf(f, "%d", &v) != 1) extra = false; else mu[m].push_back(v != 0);
		}
		if (extra)
			extra = fscanf(f, "%15s %d", key, &mi) == 2 && strcmp(key, "solos") == 0;
		for (long i = 0; extra && i < n; i++) {
			int v = 0;
			if (fscanf(f, "%d", &v) != 1) extra = false; else so[m].push_back(v != 0);
		}
	}
	// the input pair links arrived later still; a file without them is valid
	bool extra2 = extra && fscanf(f, "%15s", key) == 1 && strcmp(key, "pairlinks") == 0;
	std::vector<char> pl;
	for (long i = 0; extra2 && i < n; i++) {
		int v = 0;
		if (fscanf(f, "%d", &v) != 1) extra2 = false; else pl.push_back(v != 0);
	}
	bool extra3 = extra2 && fscanf(f, "%15s", key) == 1 && strcmp(key, "pairmono") == 0;
	std::vector<char> pm;
	for (long i = 0; extra3 && i < n; i++) {
		int v = 0;
		if (fscanf(f, "%d", &v) != 1) extra3 = false; else pm.push_back(v != 0);
	}
	int lbsrc = -1;
	bool extra4 = extra3 && fscanf(f, "%15s %d", key, &lbsrc) == 2 && strcmp(key, "loopback") == 0;
	// channel names, one line each, written only for the renamed
	std::vector<std::string> nm((size_t)n);
	if (extra4) {
		long ni = 0;
		while (fscanf(f, "%15s %ld", key, &ni) == 2 && strcmp(key, "name") == 0) {
			char rest[64] = {0};
			if (!fgets(rest, sizeof(rest), f))
				break;
			char *t = rest;
			while (*t == ' ')
				t++;
			t[strcspn(t, "\n")] = 0;
			if (ni >= 0 && ni < n && *t)
				nm[ni] = t;
		}
	}
	fclose(f);
	if (!ok)
		return false;
	for (int m = 0; m < MIXER_BUSES; m++) {
		bar_value[m] = lv[m];
		pan_value[m] = pv[m];
	}
	phase_value.assign(ph.begin(), ph.end());
	for (int p = 0; p < 3; p++)
		route_state[p] = (route[p] >= 0 && route[p] < ROUTE_SOURCES) ? route[p] : route_default[p];
	out_link[0] = link != 0;
	levels[1] = clamp01(phones);
	levels[0] = clamp01(monitor);
	current_mix = (tab >= 0 && tab < MIXER_BUSES) ? tab : 0;
	want_mix_tab = current_mix;
	for (int m = 0; m < MIXER_BUSES; m++) {
		mix_master[m] = extra ? clamp01(mm[m]) : 1.0f;
		if (extra && (long)mu[m].size() == n)
			mute_value[m].assign(mu[m].begin(), mu[m].end());
		else
			mute_value[m].assign(n, false);
		if (extra && (long)so[m].size() == n)
			solo_value[m].assign(so[m].begin(), so[m].end());
		else
			solo_value[m].assign(n, false);
	}
	if (extra2 && (long)pl.size() == n)
		chan_link.assign(pl.begin(), pl.end());
	else
		chan_link.assign(n, false);
	if (extra3 && (long)pm.size() == n)
		chan_mono.assign(pm.begin(), pm.end());
	else
		chan_mono.assign(n, false);
	route_state[3] = (extra4 && lbsrc >= 0 && lbsrc < ROUTE_SOURCES) ? lbsrc : route_default[3];
	chan_name.assign(nm.begin(), nm.end());
	return true;
}

static void load_state() { load_state_from(state_path()); }

// Presets are the same file format under a chosen name, per device, in
// the presets folder next to the state file. Recalling one is a load
// plus the same push a connect does.
static std::string presets_dir()
{
	std::string p = state_path();
	if (p.empty())
		return "";
	return p.substr(0, p.rfind('/')) + "/presets";
}

static std::string preset_path(const std::string& name)
{
	char pfx[24];
	snprintf(pfx, sizeof(pfx), "/preset-%04x-", devices[driver_indicator].usb_id);
	return presets_dir() + pfx + name + ".conf";
}

// keep names filesystem-tame: letters, digits, space, dash, underscore
static std::string sanitize_preset(const char* raw)
{
	std::string out;
	for (const char* c = raw; *c; c++)
		if (isalnum((unsigned char)*c) || *c == ' ' || *c == '-' || *c == '_')
			out += *c;
	while (!out.empty() && out.back() == ' ')
		out.pop_back();
	while (!out.empty() && out.front() == ' ')
		out.erase(out.begin());
	return out;
}

static void list_presets(std::vector<std::string>& out)
{
	out.clear();
	std::string dir = presets_dir();
	char pfx[24];
	snprintf(pfx, sizeof(pfx), "preset-%04x-", devices[driver_indicator].usb_id);
	DIR *d = opendir(dir.c_str());
	if (!d)
		return;
	struct dirent *e;
	size_t pl = strlen(pfx);
	while ((e = readdir(d)) != NULL) {
		std::string fn = e->d_name;
		if (fn.size() > pl + 5 && fn.rfind(pfx, 0) == 0 && fn.substr(fn.size() - 5) == ".conf")
			out.push_back(fn.substr(pl, fn.size() - pl - 5));
	}
	closedir(d);
	std::sort(out.begin(), out.end());
}

// Which ALSA card is this device? procfs, matched by USB id.
static int asound_card_of(uint16_t usb_id)
{
	char path[64];
	for (int card = 0; card < 32; card++) {
		snprintf(path, sizeof(path), "/proc/asound/card%d/usbid", card);
		FILE *f = fopen(path, "r");
		if (!f)
			continue;
		unsigned vid = 0, pid = 0;
		int m = fscanf(f, "%x:%x", &vid, &pid);
		fclose(f);
		if (m == 2 && vid == 0x2708 && pid == usb_id)
			return card;
	}
	return -1;
}

// The sample rate is negotiated by the kernel driver and the applications,
// not by anything BiD says over USB, so the honest source is ALSA's procfs:
// take the momentary rate of whichever stream is running. Zero means no
// stream is up, or no card was found at all.
static int read_sample_rate(uint16_t usb_id)
{
	char path[64], line[256];
	int card = asound_card_of(usb_id);
	if (card >= 0) {
		for (int stream = 0; stream < 4; stream++) {
			snprintf(path, sizeof(path), "/proc/asound/card%d/stream%d", card, stream);
			FILE *f = fopen(path, "r");
			if (!f)
				break;
			int rate = 0;
			while (fgets(line, sizeof(line), f)) {
				const char *hit = strstr(line, "Momentary freq = ");
				if (hit && sscanf(hit, "Momentary freq = %d", &rate) == 1 && rate > 0)
					break;
			}
			fclose(f);
			if (rate > 0)
				return rate;
		}
	}
	return 0;
}

// The rates the card offers, parsed from the same stream file: the union
// of every "Rates:" line, sorted. Feeds the pin-the-rate menu.
static void read_supported_rates(uint16_t usb_id, std::vector<int>& out)
{
	out.clear();
	char path[64], line[256];
	int card = asound_card_of(usb_id);
	{
		if (card < 0)
			return;
		snprintf(path, sizeof(path), "/proc/asound/card%d/stream0", card);
		FILE *f = fopen(path, "r");
		if (!f)
			return;
		while (fgets(line, sizeof(line), f)) {
			const char *hit = strstr(line, "Rates: ");
			if (!hit)
				continue;
			for (const char *c = hit + 7; *c; ) {
				int r = 0;
				if (sscanf(c, "%d", &r) == 1 && r >= 8000
				    && std::find(out.begin(), out.end(), r) == out.end())
					out.push_back(r);
				while (*c && *c != ',')
					c++;
				if (*c == ',')
					c++;
			}
		}
		fclose(f);
		std::sort(out.begin(), out.end());
		return;
	}
}

// Pinning the rate is PipeWire's decision, not the device's: the graph
// owns the clock and the hardware follows it. pw-metadata is the same
// knob the PipeWire tools use; zero unpins, and the graph goes back to
// following whatever the applications ask for.
static void force_graph_rate(int hz)
{
	char cmd[128];
	snprintf(cmd, sizeof(cmd), "pw-metadata -n settings 0 clock.force-rate %d >/dev/null 2>&1", hz);
	if (system(cmd)) {}
}

// The clock selector and its validity flags are plain ALSA controls - the
// kernel owns them, no USB protocol involved. amixer keeps BiD free of a
// libasound link, the same bargain pw-metadata strikes with PipeWire.
// Source 0 is the internal clock, 1 the optical input's.
static void read_clock_state(int card, int *src, bool *int_ok, bool *opt_ok)
{
	char cmd[160], line[256];
	*src = -1;
	*int_ok = *opt_ok = false;
	snprintf(cmd, sizeof(cmd), "amixer -c %d cget iface=MIXER,name='Audient Clock Selector Clock Source' 2>/dev/null", card);
	FILE *p = popen(cmd, "r");
	if (p) {
		while (fgets(line, sizeof(line), p)) {
			const char *v = strstr(line, ": values=");
			if (v)
				*src = atoi(v + 9);
		}
		pclose(p);
	}
	const char *names[2] = { "Internal", "Optical1" };
	bool *flags[2] = { int_ok, opt_ok };
	for (int i = 0; i < 2; i++) {
		snprintf(cmd, sizeof(cmd), "amixer -c %d cget iface=CARD,name='Audient %s Clock Validity' 2>/dev/null", card, names[i]);
		p = popen(cmd, "r");
		if (!p)
			continue;
		while (fgets(line, sizeof(line), p))
			if (strstr(line, ": values=on"))
				*flags[i] = true;
		pclose(p);
	}
}

static void set_clock_source(int card, int src)
{
	char cmd[160];
	snprintf(cmd, sizeof(cmd), "amixer -c %d cset iface=MIXER,name='Audient Clock Selector Clock Source' %d >/dev/null 2>&1", card, src);
	if (system(cmd)) {}
}

// A short kHz label: 48000 reads "48 kHz", 44100 reads "44.1 kHz".
static void khz_label(char *out, size_t n, int hz)
{
	if (hz <= 0)
		snprintf(out, n, "-- kHz");
	else if (hz % 1000 == 0)
		snprintf(out, n, "%d kHz", hz / 1000);
	else
		snprintf(out, n, "%.1f kHz", hz / 1000.0);
}

// A tray resident dies to SIGTERM at logout or shutdown; catching it turns
// that into a normal quit, so the state still gets saved on the way out.
static volatile sig_atomic_t got_signal = 0;
static void quit_signal(int) { got_signal = 1; }

// Whether this device answers reads on the monitor entity. Probed once on
// connect, so a device that stalls is asked exactly once instead of being
// polled forever.
static bool hw_readback = false;

// Metering, probed once on connect the same way. meter_raw holds the last
// block read for the sixteen matrix input nodes; the display and peak
// values are animated per frame - rise instantly, fall at a rate - which
// is what makes a meter readable instead of a flicker.
static bool meter_readback = false;
static double last_meter = 0.0;
static uint8_t meter_raw[16] = {};
static float meter_disp[16] = {};
static float meter_peak[16] = {};

// Take the front panel's word for the monitor section: those controls exist on
// the device itself and can be changed without the application ever knowing.
static void sync_state_from_device()
{
	for (int i = 0; i < TRAY_MASTER_COUNT; i++) {
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
			send_channel(i, m);
	// The matrix can have more rows than the strips show - on the iD24 the
	// last four are DAW returns 3..6 - and the hardware boots with cells
	// open. Anything without a fader gets written to silence, or it plays
	// into the buses and nothing in the app can lower it.
	for (int i = (int)bar_value[0].size(); i < devices[driver_indicator].matrix_inputs; i++)
		for (int s = 0; s < MIXER_SENDS; s++)
			set_mixer_cell(i, s, 0.0f);
	for (size_t i = 0; i < phase_value.size(); i++)
		set_phase(i, phase_value[i]);
	set_hp_volume(levels[1]);
	for (int p = 0; p < 3; p++)
		set_route_pair(p, route_state[p]);
	set_route_pair(5, route_state[3]); // the loopback pair, outs 10 and 11
}

// The whole connect ritual in one place: open the device, probe what it
// answers, pull the monitor section's state, push the mixer's. Used by the
// Connect button and by the retry that follows a suspend or a pulled cable.
static bool reconnect_pending = false;
static double next_retry = 0.0;
// whether the clock entity answers rate reads; re-probed on every connect
static bool devrate_probe = true;
// what the optical ports are set to: [0] input, [1] output; 0 = ADAT,
// 1 = S/PDIF, -1 = not read yet. The device remembers these itself, so
// they are read on connect and only written when the user flips one.
static int optical_mode[2] = { -1, -1 };
// The rest of the monitor section, read from entity 0x36 on connect. A
// negative value means the device did not answer and the control hides.
static int talk_source = -1;   // 0x10 mic 1, 0x11 mic 2, 0x12 digi 1
static int mono_mode = -1;     // 0 sum to centre, 1 left only, 2 right only
static float dim_trim = -1.0f;
static float alt_trim = -1.0f;
static bool try_connect()
{
	if (!driver_init(devices[driver_indicator].usb_id))
		return false;
	float probe;
	hw_readback = get_monitor_volume(&probe) != 0;
	if (hw_readback)
		sync_state_from_device();
	else
		// no readback: the saved level is the only truth there is, and
		// without this push the knob on screen and the hardware disagree
		// until the first nudge
		set_speaker_volume(levels[0]);
	meter_readback = get_meters(meter_raw, 16) != 0;
	for (int w = 0; w < 2; w++)
		if (!get_optical_mode(w, &optical_mode[w]))
			optical_mode[w] = -1;
	{
		unsigned char b;
		float f;
		talk_source = get_monitor_byte(0x0800, &b) ? b : -1;
		mono_mode = get_monitor_byte(0x0100, &b) ? b : -1;
		dim_trim = get_monitor_level(0x0600, &f) ? f : -1.0f;
		alt_trim = get_monitor_level(0x1700, &f) ? f : -1.0f;
	}
	devrate_probe = true;
	push_state_to_device();
	connected = true;
	return true;
}

bool toggleButton(std::string name, ImVec2 size, std::vector<bool>::reference value) {
	int mastercol = 0;
	bool state = false;
	if (value){
		ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered)); mastercol++;
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.1,0.1,0.1,1.0)); mastercol++;
	}
	// The visible half is drawn by hand, centred on its ink: this face's
	// metrics sit letters low, so the stock centring reads as misplaced in
	// small buttons. The part after ### stays the id, exactly as before.
	size_t sep = name.find("###");
	std::string vis = sep == std::string::npos ? name : name.substr(0, sep);
	std::string bid = sep == std::string::npos ? "###" + name : name.substr(sep);
	if (ImGui::Button(bid.c_str(), size)) {state = true; value.flip();};
	if (!vis.empty())
		ImGui::InkCenteredLabel(vis.c_str(), ImGui::GetColorU32(ImGuiCol_Text));
	
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
	static bool want_preset_save = false;
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
	    load_state();
	}
	signal(SIGINT, quit_signal);
	signal(SIGTERM, quit_signal);

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
		if (got_signal)
			force_quit = true;
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
		// the rate costs two file reads, so once a second is plenty
		static double last_rate = 0.0;
		static int sample_rate = 0;
		static std::vector<int> card_rates;
		static int rates_dev = -1;
		static int asound_card = -1;
		static int clock_src = -1;
		static bool clock_int_ok = false, clock_opt_ok = false;
		if (glfwGetTime() - last_rate > 1.0) {
			last_rate = glfwGetTime();
			sample_rate = read_sample_rate(devices[driver_indicator].usb_id);
			// the rate list survives replugs empty, so retry until it fills
			if (rates_dev != driver_indicator || card_rates.empty()) {
				rates_dev = driver_indicator;
				read_supported_rates(devices[driver_indicator].usb_id, card_rates);
			}
			asound_card = asound_card_of(devices[driver_indicator].usb_id);
			if (asound_card >= 0)
				read_clock_state(asound_card, &clock_src, &clock_int_ok, &clock_opt_ok);
			else
				clock_src = -1;
			// no stream running: ask the device itself, once probed willing
			if (sample_rate <= 0 && connected && devrate_probe) {
				int hz = 0;
				if (get_device_rate(&hz) && hz >= 8000 && hz <= 768000)
					sample_rate = hz;
				else
					devrate_probe = false;
			}
		}
		// A device that stops answering - suspend stales the handle, or the
		// cable came out - drops to offline and quietly retries until it is
		// back, rather than leaving faders wired to nothing.
		if (connected && driver_lost) {
			driver_shutdown();
			connected = false;
			reconnect_pending = true;
			next_retry = glfwGetTime() + 1.0;
		}
		if (reconnect_pending && glfwGetTime() > next_retry) {
			next_retry = glfwGetTime() + 2.0;
			if (try_connect())
				reconnect_pending = false;
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
			poll_idx = (poll_idx + 1) % TRAY_MASTER_COUNT;
		}
		// Meters run on their own clock, and unlike the controls they keep
		// going while a fader is being dragged - that is exactly when they
		// are being watched.
		if (connected && meter_readback && glfwGetTime() - last_meter > 0.033) {
			last_meter = glfwGetTime();
			get_meters(meter_raw, 16);
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
				if (ImGui::BeginMenu("Presets"))
				{
					static std::vector<std::string> pnames;
					list_presets(pnames);
					if (pnames.empty())
						ImGui::MenuItem("(none yet)", NULL, false, false);
					for (size_t pi = 0; pi < pnames.size(); pi++)
						if (ImGui::MenuItem(pnames[pi].c_str())) {
							// a recall is a load plus the push a connect does
							if (load_state_from(preset_path(pnames[pi])) && connected)
								push_state_to_device();
						}
					ImGui::Separator();
					if (ImGui::MenuItem("Save current as..."))
						want_preset_save = true;
					if (!pnames.empty() && ImGui::BeginMenu("Delete")) {
						for (size_t pi = 0; pi < pnames.size(); pi++)
							if (ImGui::MenuItem(pnames[pi].c_str()))
								remove(preset_path(pnames[pi]).c_str());
						ImGui::EndMenu();
					}
					ImGui::EndMenu();
				}

				ImGui::EndMenuBar();
			}
			if (want_preset_save) {
				ImGui::OpenPopup("Save preset");
				want_preset_save = false;
			}
			ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
			if (ImGui::BeginPopupModal("Save preset", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
				static char pname[32] = "";
				ImGui::TextDisabled("The whole desk, under a name.");
				ImGui::SetNextItemWidth(220.0f * main_scale);
				bool go = ImGui::InputText("##pname", pname, sizeof(pname), ImGuiInputTextFlags_EnterReturnsTrue);
				if (ImGui::Button("Save", ImVec2(104.0f * main_scale, 0)) || go) {
					std::string nm = sanitize_preset(pname);
					if (!nm.empty()) {
						save_state_to(preset_path(nm));
						pname[0] = 0;
						ImGui::CloseCurrentPopup();
					}
				}
				ImGui::SameLine();
				if (ImGui::Button("Cancel", ImVec2(104.0f * main_scale, 0))) {
					pname[0] = 0;
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndPopup();
			}
			// Main controls. They live offline too - the state file feeds them
			// and connect pushes them - so no hardware is not a blank window.
			{
				if (bar_value[0].size() == 0) {
					const device_properties &dev_init = devices[driver_indicator];
					for (size_t i = 0; i < devices[driver_indicator].mic_inputs+devices[driver_indicator].digital_inputs; i++) {
						bool is_monitor = dev_init.monitor_pair >= 0
							&& ((int)i - dev_init.mic_inputs == dev_init.monitor_pair
							 || (int)i - dev_init.mic_inputs == dev_init.monitor_pair + 1);
						phase_value.push_back(false);
						chan_link.push_back(false);
						chan_mono.push_back(false);
						chan_name.push_back(std::string());
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
							mute_value[m].push_back(false);
							solo_value[m].push_back(false);
						}
					}
				}
				// Which mix the faders edit. The other two routing sources
				// have nothing to mix: Alt is the main mix on the other
				// speakers, DAW Thru bypasses the mixer at a fixed level.
				ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(16.0f * main_scale, 8.0f * main_scale));
				if (ImGui::BeginTabBar("mixtabs")) {
					const char* mix_names[MIXER_BUSES] = { "MAIN MIX", "CUE A", "CUE B" };
					for (int m = 0; m < MIXER_BUSES; m++)
						if (ImGui::BeginTabItem(mix_names[m], nullptr, m == want_mix_tab ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None)) { current_mix = m; ImGui::EndTabItem(); }
					if (ImGui::TabItemButton("?", ImGuiTabItemFlags_Trailing | ImGuiTabItemFlags_NoTooltip)) {}
					if (ImGui::IsItemHovered())
						ImGui::SetTooltip("These are the three mixes the hardware can build.\n"
							"Alt Spkr has no page: it is the Main Mix on the other speakers.\n"
							"DAW Thru has no page: it bypasses the mixer at full level.");
					ImGui::EndTabBar();
				}
				ImGui::PopStyleVar();
				want_mix_tab = -1; // the state file's tab only needs forcing once
				// keep one line free underneath for the version string
				const float strip_h = ImGui::GetContentRegionAvail().y - ImGui::GetTextLineHeightWithSpacing();
				// strip anatomy, top to bottom: name, type-coloured chip, phase pill,
				// fader with its meter, pan pot, pan caption. The fader takes whatever
				// height the rest leaves over.
				const float fader_w = 46.0f * main_scale;
				const int chan_count = devices[driver_indicator].mic_inputs + devices[driver_indicator].digital_inputs;
				const float label_w = ImGui::CalcTextSize("DIGI 00").x;
				float chan_w = ImMax(fader_w, label_w);
				if (chan_count > 0)
					chan_w = ImClamp((ImGui::GetContentRegionAvail().x - style.ItemSpacing.x * (chan_count - 1)) / chan_count,
						ImMax(fader_w, label_w) + 10.0f * main_scale, ImMax(96.0f * main_scale, label_w));
				chan_w = (float)(int)chan_w; // whole pixels, or small pills render smeared
				const float pill_w = 20.0f * main_scale, pill_h = 18.0f * main_scale;
				const float knob_d = 34.0f * main_scale;
				const float head_fix = 50.0f * main_scale;
				const float below_fix = knob_d + 22.0f * main_scale;
				const float meter_w = 8.0f * main_scale;
				const float meter_gap = 4.0f * main_scale;
				// nine item gaps live between a strip's pieces; shorting them
				// is what once pushed the LINK bar off the panel's foot
				const float fader_h = ImMax(strip_h - (head_fix + below_fix + style.ItemSpacing.y * 9.0f + style.ScrollbarSize + 8.0f * main_scale),
					90.0f * main_scale);
				// centre an item of the given width inside the current column
				auto center_in_column = [&](float item_w) { ImGui::SetCursorPosX((float)(int)(ImGui::GetCursorPosX() + (chan_w - item_w) * 0.5f)); };
				// The meter ladder is drawn by hand beside the fader, lit from the
				// last GET_MEM block and animated here - rising instantly, falling
				// at a readable rate, with a slow peak-hold line.
				auto draw_meter = [&](ImVec2 p, float w, float h, int ch) {
					ImDrawList* dl = ImGui::GetWindowDrawList();
					float dt = ImGui::GetIO().DeltaTime;
					float target = 0.0f;
					if (connected && meter_readback && ch >= 0 && ch < 16)
						target = sqrtf((float)meter_raw[ch] / 255.0f);
					// the block carries sixteen nodes; strips past them - the larger
					// iD models - keep a dark ladder instead of walking off the end
					// of the state arrays
					float dead = 0.0f, dead_peak = 0.0f;
					const bool tracked = ch >= 0 && ch < 16;
					float &disp = tracked ? meter_disp[ch] : dead;
					float &peak = tracked ? meter_peak[ch] : dead_peak;
					disp = target > disp ? target : ImMax(0.0f, disp - dt * 1.8f);
					peak = disp > peak ? disp : ImMax(0.0f, peak - dt * 0.35f);
					dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), IM_COL32(9, 10, 13, 255), 2.5f * main_scale);
					int segs = (int)(h / (7.0f * main_scale));
					if (segs < 8) segs = 8;
					float seg_h = h / segs;
					int lit = (int)(disp * segs + 0.5f);
					for (int sg = 0; sg < segs; sg++) {
						float frac = (sg + 1.0f) / segs;
						bool on = sg < lit;
						ImU32 col;
						if (frac > 0.90f)      col = on ? IM_COL32(255, 82, 72, 255)  : IM_COL32(36, 20, 19, 255);
						else if (frac > 0.72f) col = on ? IM_COL32(255, 176, 46, 255) : IM_COL32(35, 29, 18, 255);
						else                   col = on ? IM_COL32(96, 222, 132, 255) : IM_COL32(17, 24, 20, 255);
						dl->AddRectFilled(ImVec2(p.x + 1.5f, p.y + h - (sg + 1) * seg_h + 1.5f),
							ImVec2(p.x + w - 1.5f, p.y + h - sg * seg_h - 1.0f), col, 1.5f);
					}
					if (peak > 0.02f) {
						float py = p.y + h - peak * h;
						dl->AddLine(ImVec2(p.x + 1.0f, py), ImVec2(p.x + w - 1.0f, py), IM_COL32(255, 255, 255, 150), 1.0f * main_scale);
					}
				};
				// The pan is a pot, like the consoles this mirrors; double click
				// recentres it. The caption under it says where it points.
				auto pan_knob = [&](int idx, const std::string& wid) {
					char fmt[16];
					float pv = pan_value[current_mix][idx];
					if (pv < 0.499f)      snprintf(fmt, sizeof(fmt), "L%.0f", (0.5f - pv) * 200.0f);
					else if (pv > 0.501f) snprintf(fmt, sizeof(fmt), "R%.0f", (pv - 0.5f) * 200.0f);
					else                 snprintf(fmt, sizeof(fmt), "C");
					int plm = pair_left_of(idx);
					bool mono_on = plm >= 0 && plm < (int)chan_mono.size() && chan_mono[plm];
					if (mono_on)
						snprintf(fmt, sizeof(fmt), "MONO");
					center_in_column(knob_d);
					if (mono_on)
						ImGui::BeginDisabled();
					bool moved = ImGuiKnobs::Knob(("##pan" + wid).c_str(), &pan_value[current_mix][idx], 0.0f, 1.0f, 0.004f, "",
						ImGuiKnobVariant_WiperOnly, knob_d, ImGuiKnobFlags_NoTitle | ImGuiKnobFlags_NoInput);
					if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
						pan_value[current_mix][idx] = 0.5f;
						moved = true;
					}
					hover_tip("Pan. Double click: centre");
					if (mono_on)
						ImGui::EndDisabled();
					if (moved && connected)
						send_channel(idx, current_mix);
					ImGui::PushFont(font, 13.0f);
					center_in_column(ImGui::CalcTextSize(fmt).x);
					ImGui::TextDisabled("%s", fmt);
					ImGui::PopFont();
				};
				// one whole strip. idx is the matrix input it drives, chip the type
				// colour under the name, wid keeps widget ids apart. A partner
				// channel, when given, follows the fader so a linked pair moves as
				// one; the divider is the hairline between neighbours.
				float link_row_y = 0.0f;
				auto draw_strip = [&](int idx, const std::string& label, ImU32 chip, const std::string& wid, int partner = -1, bool divider = true) {
					ImVec2 tl = ImGui::GetCursorScreenPos();
					ImGui::BeginGroup();
					ImGui::Dummy(ImVec2(chan_w, 2.0f * main_scale));
					const std::string& disp = (idx < (int)chan_name.size() && !chan_name[idx].empty()) ? chan_name[idx] : label;
					ImGui::PushFont(font, 15.0f);
					if (rename_idx == idx) {
						ImGui::SetCursorPosX((float)(int)(ImGui::GetCursorPosX() + 4.0f * main_scale));
						ImGui::SetNextItemWidth(chan_w - 8.0f * main_scale);
						if (rename_focus) {
							ImGui::SetKeyboardFocusHere();
							rename_focus = false;
						}
						bool done = ImGui::InputText("##rename", rename_buf, sizeof(rename_buf), ImGuiInputTextFlags_EnterReturnsTrue);
						if (done || ImGui::IsItemDeactivated()) {
							std::string nv = rename_buf;
							while (!nv.empty() && nv.back() == ' ')
								nv.pop_back();
							while (!nv.empty() && nv.front() == ' ')
								nv.erase(nv.begin());
							if (nv == label)
								nv.clear(); // the stock name stays the stock name
							if (idx < (int)chan_name.size())
								chan_name[idx] = nv;
							rename_idx = -1;
						}
					} else {
						center_in_column(ImGui::CalcTextSize(disp.c_str()).x);
						ImGui::TextUnformatted(disp.c_str());
						if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
							rename_idx = idx;
							snprintf(rename_buf, sizeof(rename_buf), "%s", disp.c_str());
							rename_focus = true;
						}
						hover_tip("Double click: rename");
					}
					ImGui::PopFont();
					{
						ImDrawList* dl = ImGui::GetWindowDrawList();
						float uw = 22.0f * main_scale;
						ImVec2 up = ImGui::GetCursorScreenPos();
						dl->AddRectFilled(ImVec2(up.x + (chan_w - uw) * 0.5f, up.y + 1.0f * main_scale),
							ImVec2(up.x + (chan_w + uw) * 0.5f, up.y + 4.0f * main_scale), chip, 2.0f);
					}
					ImGui::Dummy(ImVec2(chan_w, 6.0f * main_scale));
					// mute, solo and phase share one pill row: red, yellow, amber
					{
						float ps = 3.0f * main_scale;
						float row_w = pill_w * 3.0f + ps * 2.0f;
						float x0 = (float)(int)(ImGui::GetCursorPosX() + (chan_w - row_w) * 0.5f);
						ImGui::SetCursorPosX(x0);
						ImGui::PushFont(font, 13.0f);
						ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.92f, 0.30f, 0.28f, 1.00f));
						if (toggleButton("M###Mu"+wid, ImVec2(pill_w, pill_h), mute_value[current_mix][idx])) {
							if (partner >= 0)
								mute_value[current_mix][partner] = mute_value[current_mix][idx];
							if (connected) {
								send_channel(idx, current_mix);
								if (partner >= 0)
									send_channel(partner, current_mix);
							}
						}
						ImGui::PopStyleColor();
						hover_tip("Mute");
						ImGui::SameLine(); ImGui::SetCursorPosX(x0 + pill_w + ps);
						ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.95f, 0.83f, 0.25f, 1.00f));
						if (toggleButton("S###So"+wid, ImVec2(pill_w, pill_h), solo_value[current_mix][idx])) {
							if (partner >= 0)
								solo_value[current_mix][partner] = solo_value[current_mix][idx];
							if (connected) send_mix(current_mix);
						}
						ImGui::PopStyleColor();
						ImGui::PopFont();
						hover_tip("Solo: mutes everything that is not soloed");
						ImGui::SameLine(); ImGui::SetCursorPosX(x0 + (pill_w + ps) * 2.0f);
						ImGui::PushFont(audiofont, 14);
						if (toggleButton("###Phase"+wid, ImVec2(pill_w, pill_h), phase_value[idx])) {if (connected) set_phase_state(idx);};
						ImGui::PopFont();
						hover_tip("Phase invert");
					}
					ImGui::Dummy(ImVec2(chan_w, 2.0f * main_scale));
					ImGui::SetCursorPosX((float)(int)(ImGui::GetCursorPosX() + (chan_w - (fader_w + meter_gap + meter_w)) * 0.5f));
					if (ImGui::VFaderFloat(("##v"+wid).c_str(), ImVec2(fader_w, fader_h), &bar_value[current_mix][idx], 0.0f, 1.0f, "%.2f")) {
						if (partner >= 0)
							bar_value[current_mix][partner] = bar_value[current_mix][idx];
						if (connected) {
							send_channel(idx, current_mix);
							if (partner >= 0)
								send_channel(partner, current_mix);
						}
					};
					// level under the cursor while dragging, double click to unity
					if (ImGui::IsItemActive())
						ImGui::SetTooltip("%d", (int)(bar_value[current_mix][idx] * 100.0f + 0.5f));
					else
						hover_tip("Double click: unity");
					if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
						bar_value[current_mix][idx] = 1.0f;
						if (partner >= 0)
							bar_value[current_mix][partner] = 1.0f;
						if (connected) {
							send_channel(idx, current_mix);
							if (partner >= 0)
								send_channel(partner, current_mix);
						}
					}
					ImGui::SameLine(0.0f, meter_gap);
					{
						ImVec2 mp = ImGui::GetCursorScreenPos();
						ImGui::Dummy(ImVec2(meter_w, fader_h));
						draw_meter(mp, meter_w, fader_h, idx);
					}
					ImGui::Dummy(ImVec2(chan_w, 2.0f * main_scale));
					pan_knob(idx, wid);
					link_row_y = ImGui::GetCursorPosY();
					ImGui::EndGroup();
					if (divider) {
						ImDrawList* dl = ImGui::GetWindowDrawList();
						float dx = tl.x + chan_w + style.ItemSpacing.x * 0.5f;
						dl->AddLine(ImVec2(dx, tl.y + 8.0f * main_scale), ImVec2(dx, tl.y + strip_h - style.ScrollbarSize - 12.0f * main_scale),
							ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 0.05f)), 1.0f);
					}
				};

				// relinking snaps the right side back onto the left - in every mix,
				// not just the tab on screen, and the mute and solo state come along;
				// a lit LINK must never hide a split pair
				auto relink_snap = [&](int l, int r) {
					for (int m = 0; m < MIXER_BUSES; m++) {
						bar_value[m][r] = bar_value[m][l];
						mute_value[m][r] = mute_value[m][l];
						solo_value[m][r] = solo_value[m][l];
						if (connected)
							send_mix(m);
					}
				};
				// Two strips with one LINK bar under them: an input pair gets the same
				// treatment as the pinned outputs.
				auto draw_pair = [&](int li, int ri, const std::string& ll, const std::string& rl, ImU32 chip, const std::string& wl, const std::string& wr) {
					ImVec2 pp = ImGui::GetCursorPos();
					ImGui::BeginGroup();
					bool linked = chan_link[li];
					draw_strip(li, ll, chip, wl, linked ? ri : -1, false);
					ImGui::SameLine();
					draw_strip(ri, rl, chip, wr, linked ? li : -1, true);
					float lh = ImClamp(strip_h - style.ScrollbarSize - link_row_y - 8.0f * main_scale, 14.0f * main_scale, 24.0f * main_scale);
					float bw = chan_w * 2.0f + style.ItemSpacing.x - 8.0f * main_scale;
					float hw = (float)(int)((bw - 4.0f * main_scale) * 0.5f);
					ImGui::SetCursorPos(ImVec2(pp.x + 4.0f * main_scale, link_row_y + 3.0f * main_scale));
					ImGui::PushFont(font, 13.0f);
					if (toggleButton("LINK###PLnk" + wl, ImVec2(hw, lh), chan_link[li])) {
						if (chan_link[li])
							relink_snap(li, ri);
					}
					hover_tip("While lit the pair moves as one: levels, mute and solo.\nPan stays per side. Unlink to trim each side.");
					ImGui::SameLine();
					ImGui::SetCursorPosX(pp.x + 4.0f * main_scale + hw + 4.0f * main_scale);
					if (toggleButton("MONO###PMono" + wl, ImVec2(bw - hw - 4.0f * main_scale, lh), chan_mono[li])) {
						if (connected)
							for (int m = 0; m < MIXER_BUSES; m++) {
								send_channel(li, m);
								send_channel(ri, m);
							}
					}
					hover_tip("Sum the pair to mono: both sides hear both channels");
					ImGui::PopFont();
					ImGui::EndGroup();
				};

				const device_properties &dev = devices[driver_indicator];
				// type colours: mics amber, digital slate, the DAW return pair mint
				const ImU32 chip_mic  = IM_COL32(255, 163, 41, 235);
				const ImU32 chip_digi = IM_COL32(122, 150, 202, 220);
				const ImU32 chip_out  = IM_COL32(77, 208, 165, 235);
				// The DAW return pair that feeds outputs 1 and 2 lives at the far end
				// of the digital inputs, but on screen it belongs first: pinned in its
				// own slightly lifted panel, with the link bar under it, while the
				// input strips scroll past next to it.
				const int mon_idx = dev.monitor_pair >= 0 ? dev.mic_inputs + dev.monitor_pair : -1;
				if (mon_idx >= 0) {
					ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.125f, 0.135f, 0.158f, 1.00f));
					ImGui::BeginChild("PinnedOuts", ImVec2(chan_w * 2.0f + style.ItemSpacing.x, strip_h));
					draw_strip(mon_idx,     "OUT L", chip_out, "OutL", out_link[0] ? mon_idx + 1 : -1, false);
					ImGui::SameLine();
					draw_strip(mon_idx + 1, "OUT R", chip_out, "OutR", out_link[0] ? mon_idx : -1, false);
					float link_h = ImClamp(strip_h - link_row_y - 6.0f * main_scale, 16.0f * main_scale, 26.0f * main_scale);
					float bar_w = chan_w * 2.0f - style.ItemSpacing.x;
					float half_w = (float)(int)((bar_w - 4.0f * main_scale) * 0.5f);
					ImGui::SetCursorPos(ImVec2(style.ItemSpacing.x, link_row_y + 3.0f * main_scale));
					ImGui::PushFont(font, 14.0f);
					if (toggleButton("LINK", ImVec2(half_w, link_h), out_link[0])) {
						if (out_link[0])
							relink_snap(mon_idx, mon_idx + 1);
					}
					hover_tip("While lit the pair moves as one: levels, mute and solo.\nPan stays per side. Unlink to trim each side.");
					ImGui::SameLine();
					ImGui::SetCursorPosX(style.ItemSpacing.x + half_w + 4.0f * main_scale);
					if (toggleButton("MONO###OutMono", ImVec2(bar_w - half_w - 4.0f * main_scale, link_h), chan_mono[mon_idx])) {
						if (connected)
							for (int m = 0; m < MIXER_BUSES; m++) {
								send_channel(mon_idx, m);
								send_channel(mon_idx + 1, m);
							}
					}
					hover_tip("Sum the pair to mono: both sides hear both channels");
					ImGui::PopFont();
					ImGui::EndChild();
					ImGui::PopStyleColor();
					ImGui::SameLine();
				}
				ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.105f, 0.115f, 0.135f, 1.00f));
				ImGui::BeginChild("Faders", ImVec2(-(chan_w + style.ItemSpacing.x), strip_h), 0, ImGuiWindowFlags_HorizontalScrollbar);
				bool first = true;
				auto next_slot = [&]() { if (!first) ImGui::SameLine(); first = false; };
				for (int i = 0; i < dev.mic_inputs; ) {
					next_slot();
					if (i + 1 < dev.mic_inputs) {
						draw_pair(i, i + 1, "MIC " + std::to_string(i + 1), "MIC " + std::to_string(i + 2), chip_mic, "Mic" + std::to_string(i), "Mic" + std::to_string(i + 1));
						i += 2;
					} else {
						draw_strip(i, "MIC " + std::to_string(i + 1), chip_mic, "Mic" + std::to_string(i));
						i++;
					}
				}
				for (int i = 0; i < dev.digital_inputs; ) {
					if (dev.monitor_pair >= 0 && (i == dev.monitor_pair || i == dev.monitor_pair + 1)) {
						i++; // pinned on the left as OUT L / OUT R
						continue;
					}
					bool whole_pair = i + 1 < dev.digital_inputs && !(dev.monitor_pair >= 0 && i + 1 == dev.monitor_pair);
					next_slot();
					if (whole_pair) {
						draw_pair(dev.mic_inputs + i, dev.mic_inputs + i + 1, "DIGI " + std::to_string(i + 1), "DIGI " + std::to_string(i + 2), chip_digi, "Digi" + std::to_string(i), "Digi" + std::to_string(i + 1));
						i += 2;
					} else {
						draw_strip(dev.mic_inputs + i, "DIGI " + std::to_string(i + 1), chip_digi, "Digi" + std::to_string(i));
						i++;
					}
				}
				ImGui::EndChild();
				ImGui::PopStyleColor();
				ImGui::SameLine();
				// the mix master: one fader scaling everything the current mix
				// sends. The hardware has no bus fader, so it is baked into the
				// writes; the strip lives at the right edge the way consoles put it.
				ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.125f, 0.135f, 0.158f, 1.00f));
				ImGui::BeginChild("MixMaster", ImVec2(chan_w, strip_h));
				{
					const char* mnames[MIXER_BUSES] = { "MAIN", "CUE A", "CUE B" };
					ImGui::Dummy(ImVec2(chan_w, 2.0f * main_scale));
					ImGui::PushFont(font, 15.0f);
					center_in_column(ImGui::CalcTextSize(mnames[current_mix]).x);
					ImGui::TextUnformatted(mnames[current_mix]);
					ImGui::PopFont();
					{
						ImDrawList* dl = ImGui::GetWindowDrawList();
						float uw = 22.0f * main_scale;
						ImVec2 up = ImGui::GetCursorScreenPos();
						dl->AddRectFilled(ImVec2(up.x + (chan_w - uw) * 0.5f, up.y + 1.0f * main_scale),
							ImVec2(up.x + (chan_w + uw) * 0.5f, up.y + 4.0f * main_scale), IM_COL32(255, 163, 41, 235), 2.0f);
					}
					ImGui::Dummy(ImVec2(chan_w, 6.0f * main_scale));
					ImGui::Dummy(ImVec2(chan_w, pill_h)); // stay level with the strips
					ImGui::Dummy(ImVec2(chan_w, 2.0f * main_scale));
					center_in_column(fader_w);
					if (ImGui::VFaderFloat("##vMixMaster", ImVec2(fader_w, fader_h), &mix_master[current_mix], 0.0f, 1.0f, "%.2f")) {
						if (connected) send_mix(current_mix);
					};
					if (ImGui::IsItemActive())
						ImGui::SetTooltip("%d", (int)(mix_master[current_mix] * 100.0f + 0.5f));
					else
						hover_tip("This mix's master: scales everything it sends. Double click: unity");
					if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
						mix_master[current_mix] = 1.0f;
						if (connected) send_mix(current_mix);
					}
					ImGui::Dummy(ImVec2(chan_w, 2.0f * main_scale));
					{
						char vb[8];
						snprintf(vb, sizeof(vb), "%d", (int)(mix_master[current_mix] * 100.0f + 0.5f));
						ImGui::PushFont(font, 13.0f);
						center_in_column(ImGui::CalcTextSize(vb).x);
						ImGui::TextDisabled("%s", vb);
						ImGui::PopFont();
					}
				}
				ImGui::EndChild();
				ImGui::PopStyleColor();
			}
			ImGui::TextDisabled(VERSION_BID);
			//ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
			ImGui::End();

			ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + mixer_w, viewport->Pos.y));
			ImGui::SetNextWindowSize(ImVec2(panel_w, absY));
			ImGui::Begin("Monitor", nullptr, ImGuiWindowFlags_NoDecoration);
			// ---- master section: device, connection, knobs, monitor grid ----
			ImGui::Dummy(ImVec2(0, 2.0f * main_scale));
			ImGui::PushFont(font, 26.0f);
			TextCentered(devices[driver_indicator].name.c_str());
			ImGui::PopFont();
			{
				const char* st = connected ? "online" : (reconnect_pending ? "reconnecting" : "offline");
				ImGui::PushFont(font, 14.0f);
				float w = ImGui::CalcTextSize(st).x + 12.0f * main_scale;
				ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (ImGui::GetContentRegionAvail().x - w) * 0.5f);
				ImVec2 dp = ImGui::GetCursorScreenPos();
				ImGui::GetWindowDrawList()->AddCircleFilled(
					ImVec2(dp.x + 3.5f * main_scale, dp.y + ImGui::GetTextLineHeight() * 0.55f), 3.5f * main_scale,
					connected ? IM_COL32(96, 222, 132, 255) : reconnect_pending ? IM_COL32(255, 163, 41, 255) : IM_COL32(122, 128, 142, 255));
				ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 12.0f * main_scale);
				ImGui::TextDisabled("%s", st);
				ImGui::PopFont();
			}
			if (sample_rate > 0 || !card_rates.empty()) {
				char rb[24];
				khz_label(rb, sizeof(rb), sample_rate);
				ImGui::PushFont(font, 13.0f);
				float rw = ImGui::CalcTextSize(rb).x + style.FramePadding.x * 2.0f;
				ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (ImGui::GetContentRegionAvail().x - rw) * 0.5f);
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 1.0f, 1.0f, 0.06f));
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 1.0f, 1.0f, 0.10f));
				ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
				if (ImGui::SmallButton(rb))
					ImGui::OpenPopup("graphrate");
				ImGui::PopStyleColor(4);
				ImGui::PopFont();
				hover_tip("The sample rate the graph runs at. Click to pin another\nthrough PipeWire, or hand the choice back to the applications.");
				if (ImGui::BeginPopup("graphrate")) {
					for (int r : card_rates) {
						char lb[24];
						khz_label(lb, sizeof(lb), r);
						if (ImGui::MenuItem(lb, NULL, r == sample_rate))
							force_graph_rate(r);
					}
					ImGui::Separator();
					if (ImGui::MenuItem("Follow the applications"))
						force_graph_rate(0);
					ImGui::EndPopup();
				}
			}
			if (clock_src >= 0) {
				const bool cur_ok = clock_src == 1 ? clock_opt_ok : clock_int_ok;
				const char* cname = clock_src == 1 ? "Optical" : "Internal";
				ImGui::PushFont(font, 13.0f);
				float cw = ImGui::CalcTextSize(cname).x + style.FramePadding.x * 2.0f + 10.0f * main_scale;
				ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (ImGui::GetContentRegionAvail().x - cw) * 0.5f);
				ImVec2 cp = ImGui::GetCursorScreenPos();
				ImGui::GetWindowDrawList()->AddCircleFilled(
					ImVec2(cp.x + 3.0f * main_scale, cp.y + ImGui::GetTextLineHeight() * 0.62f), 3.0f * main_scale,
					cur_ok ? IM_COL32(96, 222, 132, 255) : IM_COL32(255, 82, 72, 255));
				ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 10.0f * main_scale);
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 1.0f, 1.0f, 0.06f));
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 1.0f, 1.0f, 0.10f));
				ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
				if (ImGui::SmallButton(cname))
					ImGui::OpenPopup("clocksrc");
				ImGui::PopStyleColor(4);
				ImGui::PopFont();
				hover_tip("The clock the converters follow, with a light for its signal.\nOptical audio with the clock on Internal drifts and crackles -\nslave to Optical when recording the optical input.");
				if (ImGui::BeginPopup("clocksrc")) {
					if (ImGui::MenuItem(clock_int_ok ? "Internal" : "Internal (no signal)", NULL, clock_src == 0))
						set_clock_source(asound_card, 0);
					if (ImGui::MenuItem(clock_opt_ok ? "Optical" : "Optical (no signal)", NULL, clock_src == 1))
						set_clock_source(asound_card, 1);
					ImGui::EndPopup();
				}
			}
			const bool call_to_action = !connected;
			if (call_to_action) {
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.00f, 0.64f, 0.16f, 1.00f));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.00f, 0.74f, 0.32f, 1.00f));
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.00f, 0.80f, 0.45f, 1.00f));
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.11f, 0.09f, 0.06f, 1.00f));
			}
			if (ImGui::Button(connected ? "Disconnect" : "Connect", ImVec2(ImGui::GetContentRegionAvail().x, 34.0f * main_scale))) {
				if (connected) {
					connected = false;
					reconnect_pending = false; // asked to let go: no quiet retry
					driver_shutdown();
				}
				else if (try_connect())
					reconnect_pending = false;
				else
					ImGui::OpenPopup("No connection possible");
			};
			if (call_to_action)
				ImGui::PopStyleColor(4);
			// Always center this window when appearing
			ImVec2 center = ImGui::GetMainViewport()->GetCenter();
			ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

			if (ImGui::BeginPopupModal("No connection possible", NULL, ImGuiWindowFlags_AlwaysAutoResize))
			{
				ImGui::Text("USB Device can not be opened.");
				ImGui::Text("Make sure you have selected the correct driver and your usb permissions are correct.");
				ImGui::Text("This can either be done by adding the usb device to the udev rules,");
				ImGui::Text("or running BiD with sudo permissions.");
				ImGui::Dummy(ImVec2(10, 20 * main_scale));
				ImGui::Separator();
				if (ImGui::Button("OK", ImVec2(120 * main_scale, 0))) { ImGui::CloseCurrentPopup(); }
				ImGui::SetItemDefaultFocus();
				ImGui::EndPopup();
			}

			// knobs: the monitor level is the hero, phones under it, and the
			// toggle grid stays anchored to the bottom of the panel
			auto caps_label = [&](const char* cs, float pt) {
				ImGui::PushFont(font, pt);
				ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(cs).x) * 0.5f);
				ImGui::TextDisabled("%s", cs);
				ImGui::PopFont();
			};
			const float btn_h = 42.0f * main_scale;
			const float toggles_h = btn_h * 2.0f + style.ItemSpacing.y;
			const float knob_big = 104.0f * main_scale;
			const float knob_small = 72.0f * main_scale;
			float knob_gap = ImMax((absY - ImGui::GetCursorPosY() - toggles_h - style.WindowPadding.y
					- knob_big - knob_small - ImGui::GetTextLineHeightWithSpacing() * 4.0f) / 4.0f,
				4.0f * main_scale);
			ImGui::Dummy(ImVec2(0, knob_gap));
			caps_label("MONITOR", 15.0f);
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (ImGui::GetContentRegionAvail().x - knob_big) * 0.5f);
			if (ImGuiKnobs::Knob("##monitor", &levels[0], 0.0f, 1.0f, 0.004f, "", ImGuiKnobVariant_WiperOnly, knob_big,
					ImGuiKnobFlags_NoTitle | ImGuiKnobFlags_NoInput)) {
				if (connected) set_speaker_volume(levels[0]);
			}
			hover_tip("Monitor level: follows the hardware knob");
			{ char vb[8]; snprintf(vb, sizeof(vb), "%d", (int)(levels[0] * 100.0f + 0.5f)); caps_label(vb, 17.0f); }
			ImGui::Dummy(ImVec2(0, knob_gap));
			caps_label("PHONES", 15.0f);
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (ImGui::GetContentRegionAvail().x - knob_small) * 0.5f);
			if (ImGuiKnobs::Knob("##phones", &levels[1], 0.0f, 1.0f, 0.004f, "", ImGuiKnobVariant_WiperOnly, knob_small,
					ImGuiKnobFlags_NoTitle | ImGuiKnobFlags_NoInput)) {
				if (connected) set_hp_volume(levels[1]);
			}
			hover_tip("Headphone level");
			{ char vb[8]; snprintf(vb, sizeof(vb), "%d", (int)(levels[1] * 100.0f + 0.5f)); caps_label(vb, 17.0f); }

			ImGui::SetCursorPosY(ImMax(ImGui::GetCursorPosY(), absY - toggles_h - style.WindowPadding.y));
			const float grid_w = ImGui::GetContentRegionAvail().x;
			const float btn_w = (float)(int)((grid_w - style.ItemSpacing.x * 2.0f) / 3.0f);
			auto grid_row = [&]() { ImGui::SetCursorPosX(style.WindowPadding.x + ImMax(0.0f, (grid_w - (btn_w * 3.0f + style.ItemSpacing.x * 2.0f)) * 0.5f)); };
			ImGui::PushFont(font, 16.0f);
			ImGui::BeginGroup();
			grid_row();
			if (toggleButton("DIM", ImVec2(btn_w, btn_h), master_bools[0])) { if (connected) {set_bool_state(0);} tray_set_master(0, master_bools[0]);};
			if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
				ImGui::OpenPopup("dimtrim");
			hover_tip("Dim the monitor level. Right click: how far it drops");
			if (ImGui::BeginPopup("dimtrim")) {
				ImGui::TextDisabled("Dim drops the monitors to");
				if (dim_trim >= 0.0f) {
					int pct = (int)(dim_trim * 100.0f + 0.5f);
					ImGui::SetNextItemWidth(160.0f * main_scale);
					if (ImGui::SliderInt("##dimtrim", &pct, 0, 100, "%d%%")) {
						dim_trim = pct / 100.0f;
						if (connected) set_monitor_level(0x0600, dim_trim);
					}
				} else
					ImGui::TextDisabled(connected ? "no answer from the device" : "connect first");
				ImGui::EndPopup();
			}
			ImGui::SameLine();
			if (toggleButton("ALT", ImVec2(btn_w, btn_h), master_bools[1])) { if (connected) {set_bool_state(1);} tray_set_master(1, master_bools[1]);};
			if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
				ImGui::OpenPopup("alttrim");
			hover_tip("Switch to the alternate speakers. Right click: their trim");
			if (ImGui::BeginPopup("alttrim")) {
				ImGui::TextDisabled("Alternate speakers' trim");
				if (alt_trim >= 0.0f) {
					int pct = (int)(alt_trim * 100.0f + 0.5f);
					ImGui::SetNextItemWidth(160.0f * main_scale);
					if (ImGui::SliderInt("##alttrim", &pct, 0, 100, "%d%%")) {
						alt_trim = pct / 100.0f;
						if (connected) set_monitor_level(0x1700, alt_trim);
					}
				} else
					ImGui::TextDisabled(connected ? "no answer from the device" : "connect first");
				ImGui::EndPopup();
			}
			ImGui::SameLine();
			if (toggleButton("TALK", ImVec2(btn_w, btn_h), master_bools[2])) { if (connected) {set_bool_state(2);} tray_set_master(2, master_bools[2]);};
			if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
				ImGui::OpenPopup("talksrc");
			hover_tip("Talkback to the cues. Right click: what it talks with");
			if (ImGui::BeginPopup("talksrc")) {
				const char* tnames[3] = { "Mic 1", "Mic 2", "Digi 1" };
				for (int k = 0; k < 3; k++)
					if (ImGui::MenuItem(tnames[k], NULL, talk_source == 0x10 + k, connected)) {
						talk_source = 0x10 + k;
						set_monitor_byte(0x0800, (unsigned char)talk_source);
					}
				ImGui::EndPopup();
			}
			grid_row();
			if (toggleButton("PHASE", ImVec2(btn_w, btn_h), master_bools[3])) { if (connected) {set_bool_state(3);} tray_set_master(3, master_bools[3]);};
			hover_tip("Invert the monitors' phase");
			ImGui::SameLine();
			{
				std::string mono_face = mono_mode == 1 ? "MONO L###MONO" : mono_mode == 2 ? "MONO R###MONO" : "MONO";
				if (toggleButton(mono_face, ImVec2(btn_w, btn_h), master_bools[4])) { if (connected) {set_bool_state(4);} tray_set_master(4, master_bools[4]);};
			}
			if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
				ImGui::OpenPopup("monomode");
			hover_tip("Sum the monitors to mono. Right click: centre, left or right only");
			if (ImGui::BeginPopup("monomode")) {
				const char* mnames[3] = { "Sum to centre", "Left only", "Right only" };
				for (int k = 0; k < 3; k++)
					if (ImGui::MenuItem(mnames[k], NULL, mono_mode == k, connected)) {
						mono_mode = k;
						set_monitor_byte(0x0100, (unsigned char)k);
					}
				ImGui::EndPopup();
			}
			ImGui::SameLine();
			if (toggleButton("CUT", ImVec2(btn_w, btn_h), master_bools[5])) { if (connected) {set_bool_state(5);} tray_set_master(5, master_bools[5]);};
			hover_tip("Mute the monitors");
			ImGui::EndGroup();
			ImGui::PopFont();


			ImGui::End();
		}

		// 3. Show another simple window.
		if (show_another_window)
		{
			// A floor on the size, so the window is usable even when imgui.ini
			// carries a size saved at a different display scale.
			// sized to its content and not resizable: there is nothing in it
			// worth stretching
			ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
			ImGui::Begin("Driver Select", &show_another_window, ImGuiWindowFlags_AlwaysAutoResize);
	        
	        //const char* combo_preview_value = items[item_selected_idx];
	        ImGui::SetNextItemWidth(240.0f * main_scale);
	        if (ImGui::BeginCombo("Interface type", devices[driver_indicator].name.c_str()))
	        {
	            for (int n = 0; n < devices.size(); n++)
	            {
	                const bool is_selected = (driver_indicator == n);
	                if (ImGui::Selectable(devices[n].name.c_str(), is_selected)) {
	                    if (connected) {
	                    	// the handle still points at the outgoing device: left
	                    	// connected, every write would land on the wrong box
	                    	connected = false;
	                    	driver_shutdown();
	                    }
	                    reconnect_pending = false; // and stop hunting for it
	                    save_state(); // keep the outgoing device's edits
	                    driver_indicator = n;
	                    reset_mixes();
	                    phase_value.clear();
	                    reset_routing();
	                    load_state();
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
			ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
			ImGui::Begin("Routing", &show_routing, ImGuiWindowFlags_AlwaysAutoResize);
			// One source per physical output pair, matching how the official
			// app treats routing. The rows are the routing outputs in device
			// order: 0/1, 2/3, then the phones on 4/5. The full story lives
			// in the (?) so the table is not buried under a paragraph.
			const char* pair_names[]   = { "Out 1+2", "Out 3+4", "Phones", "Loopback" };
			const char* source_names[] = { "Main Mix", "Alt Spkr", "Cue A", "Cue B", "DAW Thru" };
			ImGui::TextDisabled("Each output pair plays one source.");
			ImGui::SameLine();
			ImGui::TextDisabled("(?)");
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip(
					"Main Mix is the monitor section's feed: the volume knob, Dim and Cut\n"
					"apply on any output carrying it, phones included. Alt Spkr is that same\n"
					"feed for a second set of speakers, switched in with Alt. The cues are\n"
					"separate mixes, clear of the monitor section, so the speaker buttons\n"
					"leave them alone and the Phones dial sets the phones' level. DAW Thru\n"
					"comes straight from the computer at full level - no control at all.\n"
					"\n"
					"Loopback is not a jack: whatever it plays arrives back in the\n"
					"computer as inputs 11+12, ready to record or stream. On that row,\n"
					"DAW Thru means playback channels 11+12 looped straight back.");
			ImGui::Spacing();

			if (ImGui::BeginTable("routing", 1 + ROUTE_SOURCES, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInnerH))
			{
				ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed,
					ImGui::CalcTextSize("Out 0+0").x + style.CellPadding.x * 2.0f);
				for (int s = 0; s < ROUTE_SOURCES; s++)
					ImGui::TableSetupColumn(source_names[s], ImGuiTableColumnFlags_WidthFixed,
						ImGui::CalcTextSize(source_names[s]).x + style.CellPadding.x * 2.0f + 8.0f * main_scale);
				ImGui::TableHeadersRow();
				for (int pair = 0; pair < 4; pair++)
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
								// the loopback row lives on hardware pair 5: outs 10 and 11
								if (connected)
									set_route_pair(pair == 3 ? 5 : pair, s);
							};
							ImGui::PopID();
						}
					ImGui::PopID();
				}
				ImGui::EndTable();
			}
			ImGui::Spacing();
			ImGui::TextDisabled("Optical ports");
			ImGui::SameLine();
			ImGui::TextDisabled("(?)");
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("ADAT carries Digi 1..8; S/PDIF is a stereo pair. The device\nremembers the modes itself, so BiD reads them on connect and\nonly writes when a mode is flipped here.");
			if (!connected) {
				ImGui::TextDisabled("Connect to read and change the port modes.");
			} else {
				const char* port_names[2] = { "In", "Out" };
				for (int w = 0; w < 2; w++) {
					ImGui::PushID(200 + w);
					ImGui::AlignTextToFramePadding();
					ImGui::TextUnformatted(port_names[w]);
					ImGui::SameLine(56.0f * main_scale);
					if (ImGui::RadioButton("ADAT", optical_mode[w] == 0)) {
						optical_mode[w] = 0;
						set_optical_mode(w, 0);
					}
					ImGui::SameLine();
					if (ImGui::RadioButton("S/PDIF", optical_mode[w] == 1)) {
						optical_mode[w] = 1;
						set_optical_mode(w, 1);
					}
					if (optical_mode[w] < 0) {
						ImGui::SameLine();
						ImGui::TextDisabled("- no answer");
					}
					ImGui::PopID();
				}
			}
			ImGui::Spacing();
			if (ImGui::Button("Reset to defaults")) {
				reset_routing();
				if (connected)
					for (int p = 0; p < 4; p++)
						set_route_pair(p == 3 ? 5 : p, route_state[p]);
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
	save_state();
	driver_shutdown(); //just in case disconnect
	// Cleanup (window_close() is a no-op when already hidden to the tray)
	window_close();
	ImGui::DestroyContext();

	glfwTerminate();

	return 0;
}
