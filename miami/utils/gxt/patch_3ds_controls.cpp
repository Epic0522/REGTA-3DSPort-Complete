#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static std::vector<uint8_t>
utf16le(const char *text)
{
	std::vector<uint8_t> out;
	for(const unsigned char *p = (const unsigned char*)text; *p; p++){
		out.push_back(*p);
		out.push_back(0);
	}
	out.push_back(0);
	out.push_back(0);
	return out;
}

static int
replaceInPlace(std::vector<uint8_t> &data, const char *oldText, const char *newText)
{
	const std::vector<uint8_t> oldBytes = utf16le(oldText);
	const std::vector<uint8_t> newBytes = utf16le(newText);
	if(newBytes.size() > oldBytes.size())
		return 0;

	int changed = 0;
	for(size_t i = 0; i + oldBytes.size() <= data.size(); i++){
		if(memcmp(data.data() + i, oldBytes.data(), oldBytes.size()) == 0){
			memcpy(data.data() + i, newBytes.data(), newBytes.size());
			memset(data.data() + i + newBytes.size(), 0,
				oldBytes.size() - newBytes.size());
			changed++;
			i += oldBytes.size() - 1;
		}
	}
	return changed;
}

int
main(int argc, char **argv)
{
	if(argc != 2){
		fprintf(stderr, "usage: %s AMERICAN.GXT\n", argv[0]);
		return 2;
	}

	FILE *file = fopen(argv[1], "rb");
	if(file == nullptr){
		perror(argv[1]);
		return 1;
	}
	fseek(file, 0, SEEK_END);
	long length = ftell(file);
	fseek(file, 0, SEEK_SET);
	std::vector<uint8_t> data((size_t)length);
	if(fread(data.data(), 1, data.size(), file) != data.size()){
		fclose(file);
		return 1;
	}
	fclose(file);

	struct Replacement { const char *oldText, *newText; };
	static const Replacement replacements[] = {
		{ "Push the right analog stick up to ~h~accelerate.",
		  "Push C-STICK up to ~h~accelerate." },
		{ "Pull the right analog stick back to brake, or to reverse if the vehicle has stopped.",
		  "Pull C-STICK back to brake, or to reverse if the vehicle has stopped." },
		{ "Use the right analog stick to accelerate, pull back on the left analog stick to climb, push forwards to descend. Left and right to turn.",
		  "Use C-STICK to accelerate, pull back on CIRCLE PAD to climb, push forwards to descend. Left and right to turn." },
		{ "Pushing ~h~back on the analog stick ~w~decreases the rotor speed, causing the helicopter to~h~ descend.",
		  "Pulling ~h~back on C-STICK ~w~decreases the rotor speed, causing the helicopter to~h~ descend." },
		{ "Pushing ~h~forward on the analog stick ~w~increases the rotor speed, causing the helicopter to ~h~ascend.",
		  "Pushing ~h~forward on C-STICK ~w~increases the rotor speed, causing the helicopter to ~h~ascend." },
		{ "Press the ~h~~k~~GO_LEFT~~w~, and the ~h~~k~~GO_RIGHT~~w~, to steer the vehicle.",
		  "Use the CIRCLE PAD to steer the vehicle." },
	};

	int changed = 0;
	for(const Replacement &replacement : replacements)
		changed += replaceInPlace(data, replacement.oldText, replacement.newText);

	file = fopen(argv[1], "wb");
	if(file == nullptr){
		perror(argv[1]);
		return 1;
	}
	const bool ok = fwrite(data.data(), 1, data.size(), file) == data.size();
	fclose(file);
	printf("patched %d control strings in %s\n", changed, argv[1]);
	return ok ? 0 : 1;
}
