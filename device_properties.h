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
};

static std::vector<device_properties> devices;

void setup_devices()
{
	struct device_properties iD4;
	iD4.name = "iD4";
	iD4.usb_id = 0x0003;
	iD4.mic_inputs = 1;
	iD4.digital_inputs = 0;
	iD4.outputs = 2;
	iD4.digital_outputs = 0;
	devices.push_back(iD4);

	struct device_properties iD4MKII;
	iD4MKII.name = "iD4 MKII";
	iD4MKII.usb_id = 0x0009;
	iD4MKII.mic_inputs = 1;
	iD4MKII.digital_inputs = 0;
	iD4MKII.outputs = 2;
	iD4MKII.digital_outputs = 0;
	devices.push_back(iD4MKII);

	struct device_properties iD14;
	iD14.name = "iD14";
	iD14.usb_id = 0x0002;
	iD14.mic_inputs = 2;
	iD14.digital_inputs = 8;
	iD14.outputs = 4;
	iD14.digital_outputs = 0;
	devices.push_back(iD14);

	struct device_properties iD14MKII;
	iD14MKII.name = "iD14 MKII";
	iD14MKII.usb_id = 0x0008;
	iD14MKII.mic_inputs = 2;
	iD14MKII.digital_inputs = 8;
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
	devices.push_back(iD24);

	struct device_properties iD44;
	iD44.name = "iD44";
	iD44.usb_id = 0x0005;
	iD44.mic_inputs = 4;
	iD44.digital_inputs = 16;
	iD44.outputs = 4;
	iD44.digital_outputs = 16;
	iD44.inserts = 2;
	devices.push_back(iD44);

	struct device_properties iD44MKII;
	iD44MKII.name = "iD44 MKII";
	iD44MKII.usb_id = 0x000b;
	iD44MKII.mic_inputs = 4;
	iD44MKII.digital_inputs = 16;
	iD44MKII.outputs = 4;
	iD44MKII.digital_outputs = 16;
	iD44MKII.inserts = 2;
	devices.push_back(iD44MKII);

	struct device_properties iD48;
	iD48.name = "iD48";
	iD48.usb_id = 0x0012;
	iD48.mic_inputs = 8;
	iD48.digital_inputs = 16;
	iD48.outputs = 4;
	iD48.digital_outputs = 16;
	iD48.inserts = 8;
	devices.push_back(iD48);
}

#endif