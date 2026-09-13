#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

static std::vector<uint8_t>
utf16le(const char *text)
{
	std::vector<uint8_t> out;
	for(const unsigned char *p = (const unsigned char*)text; *p; p++) {
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
	for(size_t i = 0; i + oldBytes.size() <= data.size(); i++) {
		if(memcmp(data.data() + i, oldBytes.data(), oldBytes.size()) == 0) {
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
	if(argc != 2) {
		fprintf(stderr, "usage: %s AMERICAN.GXT\n", argv[0]);
		return 2;
	}
	FILE *file = fopen(argv[1], "rb");
	if(file == nullptr) {
		perror(argv[1]);
		return 1;
	}
	fseek(file, 0, SEEK_END);
	long length = ftell(file);
	fseek(file, 0, SEEK_SET);
	std::vector<uint8_t> data((size_t)length);
	if(fread(data.data(), 1, data.size(), file) != data.size()) {
		fclose(file);
		return 1;
	}
	fclose(file);

	struct Replacement { const char *oldText, *newText; };
	static const Replacement replacements[] = {
		{ "Push the~h~ right analog stick~w~ up to ~h~accelerate.",
		  "Push the~h~ C-STICK~w~ up to ~h~accelerate." },
		{ "Pull the ~h~right analog stick~w~ back to ~h~brake~w~, or to ~h~reverse~w~ if the vehicle has stopped.",
		  "Pull the ~h~C-STICK~w~ back to ~h~brake~w~, or to ~h~reverse~w~ if the vehicle has stopped." },
		{ "This is the ~h~radar~w~. Use it to navigate the city, follow the ~h~blip~w~ on the ~h~radar~w~ to find the hideout!",
		  "The lower screen shows the ~h~radar~w~. Use it to navigate the city. Follow the ~h~blip~w~ to find the hideout!" },
	};
	int changed = 0;
	for(const Replacement &replacement : replacements)
		changed += replaceInPlace(data, replacement.oldText, replacement.newText);

	file = fopen(argv[1], "wb");
	if(file == nullptr) {
		perror(argv[1]);
		return 1;
	}
	const bool ok = fwrite(data.data(), 1, data.size(), file) == data.size();
	fclose(file);
	printf("patched %d control strings in %s\n", changed, argv[1]);
	return ok ? 0 : 1;
}
