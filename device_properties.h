#ifndef _H_DEVICEPROPERTIES_H_
#define _H_DEVICEPROPERTIES_H_

#include <string>
#include <vector>

struct device_properties {
	std::string name;
	uint16_t usb_id;
	int mic_inputs;
	int digital_inputs;
	int outputs;
	int digital_outputs;
	int inserts = 0;
	// Index, among the digital inputs, of the left half of the pair that feeds
	// the monitor outputs. That pair is the one place we know is stereo, so it
	// is the only one panned apart by default. -1 when we do not know, in
	// which case every digital input starts centred.
	int monitor_pair = -1;
	// Rows in the mixer matrix, when known to be more than the channels the
	// app shows. The hardware boots with cells open, so rows without a fader
	// are written to silence on connect; 0 means no such extra rows.
	int matrix_inputs = 0;

	// Whether this model's protocol has been confirmed on real hardware.
	// Only a verified device gets the full state pushed on connect: the
	// entity and cell addresses are the iD24's, and writing them blindly
	// into a different model can silence its mixer, with only a replug to
	// undo it - which is exactly what happened to an iD14 MKII (issue #26).
	bool protocol_verified = false;

	// What the model actually has. Decoded from the official app by the
	// Monix project (their docs/DEVICES.md); the iD24 numbers are the ones
	// confirmed here on hardware. A model that lacks a feature has its
	// controls hidden or greyed rather than offered and silently ignored.
	int cue_mixes = 2;             // stereo cue buses besides the main mix
	bool has_alt = true;           // alternate speaker switching
	bool has_optical_out = true;
	bool has_loopback = false;     // only the iD24 and iD48 define one
	int daw_returns = 6;           // matrix rows fed by the computer
};

static std::vector<device_properties> devices;

void setup_devices()
{
	struct device_properties iD4;
	iD4.name = "iD4";
	iD4.usb_id = 0x0003;
	iD4.mic_inputs = 1;
	iD4.digital_inputs = 0;
	// no mixer, no routing: a fixed 2-in/2-out interface
	iD4.cue_mixes = 0;
	iD4.has_alt = false;
	iD4.has_optical_out = false;
	iD4.daw_returns = 0;
	iD4.outputs = 2;
	iD4.digital_outputs = 0;
	devices.push_back(iD4);

	struct device_properties iD4MKII;
	iD4MKII.name = "iD4 MKII";
	iD4MKII.usb_id = 0x0009;
	iD4MKII.mic_inputs = 1;
	iD4MKII.digital_inputs = 0;
	iD4MKII.cue_mixes = 0;
	iD4MKII.has_alt = false;
	iD4MKII.has_optical_out = false;
	iD4MKII.daw_returns = 0;
	iD4MKII.outputs = 2;
	iD4MKII.digital_outputs = 0;
	devices.push_back(iD4MKII);

	struct device_properties iD14;
	iD14.name = "iD14";
	iD14.usb_id = 0x0002;
	iD14.mic_inputs = 2;
	// The iD14 family is a smaller machine than the iD24: fourteen mixer
	// nodes ending in four DAW returns, one cue mix instead of two, no
	// alternate speakers and no optical output. Digital inputs 9 and 10
	// are DAW 1+2, shown pinned as the output pair like on the iD24.
	iD14.digital_inputs = 10;
	iD14.monitor_pair = 8;
	iD14.matrix_inputs = 14;
	iD14.cue_mixes = 1;
	iD14.has_alt = false;
	iD14.has_optical_out = false;
	iD14.daw_returns = 4;
	iD14.outputs = 4;
	iD14.digital_outputs = 0;
	devices.push_back(iD14);

	struct device_properties iD14MKII;
	iD14MKII.name = "iD14 MKII";
	iD14MKII.usb_id = 0x0008;
	iD14MKII.mic_inputs = 2;
	// The iD14 family is a smaller machine than the iD24: fourteen mixer
	// nodes ending in four DAW returns, one cue mix instead of two, no
	// alternate speakers and no optical output. Digital inputs 9 and 10
	// are DAW 1+2, shown pinned as the output pair like on the iD24.
	iD14MKII.digital_inputs = 10;
	iD14MKII.monitor_pair = 8;
	iD14MKII.matrix_inputs = 14;
	iD14MKII.cue_mixes = 1;
	iD14MKII.has_alt = false;
	iD14MKII.has_optical_out = false;
	iD14MKII.daw_returns = 4;
	iD14MKII.outputs = 4;
	iD14MKII.digital_outputs = 0;
	devices.push_back(iD14MKII);

	struct device_properties iD22;
	iD22.name = "iD22";
	iD22.usb_id = 0x0001;
	iD22.mic_inputs = 2;
	iD22.digital_inputs = 8;
	iD22.outputs = 4;
	iD22.digital_outputs = 8;
	iD22.inserts = 2;
	iD22.matrix_inputs = 16;
	iD22.daw_returns = 6;
	devices.push_back(iD22);

	struct device_properties iD24;
	iD24.name = "iD24";
	iD24.usb_id = 0x000d;
	iD24.mic_inputs = 2;
	iD24.digital_inputs = 10;
	iD24.outputs = 4;
	iD24.digital_outputs = 14;
	iD24.inserts = 2;
	// verified on hardware: digital inputs 9 and 10 are the pair that comes
	// out of outputs 1 and 2, so they are the monitor left and right
	iD24.monitor_pair = 8;
	// 16 matrix rows: mics, eight digi, then DAW returns 1..6 - the last
	// four have no fader here and get silenced on connect
	iD24.matrix_inputs = 16;
	iD24.protocol_verified = true;
	iD24.has_loopback = true; // outputs 10 and 11, back to the host
	devices.push_back(iD24);

	struct device_properties iD44;
	iD44.name = "iD44";
	iD44.usb_id = 0x0005;
	// thirty nodes and four cues; BiD carries two cue pages, so the extra
	// cues are out of reach until the state file grows to hold them
	iD44.matrix_inputs = 30;
	iD44.daw_returns = 10;
	iD44.mic_inputs = 4;
	iD44.digital_inputs = 16;
	iD44.outputs = 4;
	iD44.digital_outputs = 16;
	iD44.inserts = 2;
	devices.push_back(iD44);

	struct device_properties iD44MKII;
	iD44MKII.name = "iD44 MKII";
	iD44MKII.usb_id = 0x000b;
	iD44MKII.matrix_inputs = 30;
	iD44MKII.daw_returns = 10;
	iD44MKII.mic_inputs = 4;
	iD44MKII.digital_inputs = 16;
	iD44MKII.outputs = 4;
	iD44MKII.digital_outputs = 16;
	iD44MKII.inserts = 2;
	devices.push_back(iD44MKII);

	struct device_properties iD48;
	iD48.name = "iD48";
	iD48.usb_id = 0x0012;
	iD48.matrix_inputs = 32;
	iD48.daw_returns = 8;
	iD48.has_loopback = true; // output 0x16
	iD48.mic_inputs = 8;
	iD48.digital_inputs = 16;
	iD48.outputs = 4;
	iD48.digital_outputs = 16;
	iD48.inserts = 8;
	devices.push_back(iD48);
}

#endif