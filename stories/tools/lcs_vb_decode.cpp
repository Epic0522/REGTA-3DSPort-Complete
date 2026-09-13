#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

constexpr std::size_t kBlockSize = 0x2000;
constexpr std::size_t kLineSize = 0x10;
constexpr int kSamplesPerLine = 28;

struct VagState {
	double s1 = 0.0;
	double s2 = 0.0;
};

const double kFilter[5][2] = {
	{0.0, 0.0},
	{60.0 / 64.0, 0.0},
	{115.0 / 64.0, -52.0 / 64.0},
	{98.0 / 64.0, -55.0 / 64.0},
	{122.0 / 64.0, -60.0 / 64.0},
};

void DecodeLine(const uint8_t *input, VagState &state, int16_t *output)
{
	const int predictor = std::min<int>(input[0] >> 4, 4);
	const int shift = input[0] & 0x0F;
	if (input[1] == 7) {
		std::fill(output, output + kSamplesPerLine, 0);
		return;
	}

	for (int i = 0; i < kSamplesPerLine; i++) {
		const uint8_t packed = input[2 + i / 2];
		int16_t nibble = i & 1 ? int16_t((packed & 0xF0) << 8)
		                           : int16_t((packed & 0x0F) << 12);
		double sample = double(nibble >> shift)
		              + state.s1 * kFilter[predictor][0]
		              + state.s2 * kFilter[predictor][1];
		state.s2 = state.s1;
		state.s1 = sample;
		int value = int(sample + (sample >= 0.0 ? 0.5 : -0.5));
		output[i] = int16_t(std::max(-32768, std::min(32767, value)));
	}
}

} // namespace

int main(int argc, char **argv)
{
	if (argc != 2) {
		std::fprintf(stderr, "usage: %s input.VB\n", argv[0]);
		return 2;
	}

	FILE *input = std::fopen(argv[1], "rb");
	if (!input) {
		std::perror(argv[1]);
		return 1;
	}

	std::vector<uint8_t> leftBlock(kBlockSize);
	std::vector<uint8_t> rightBlock(kBlockSize);
	VagState leftState;
	VagState rightState;
	int16_t left[kSamplesPerLine];
	int16_t right[kSamplesPerLine];
	int16_t mono[kSamplesPerLine];

	while (std::fread(leftBlock.data(), 1, kBlockSize, input) == kBlockSize &&
	       std::fread(rightBlock.data(), 1, kBlockSize, input) == kBlockSize) {
		for (std::size_t offset = 0; offset < kBlockSize; offset += kLineSize) {
			DecodeLine(leftBlock.data() + offset, leftState, left);
			DecodeLine(rightBlock.data() + offset, rightState, right);
			for (int i = 0; i < kSamplesPerLine; i++)
				mono[i] = int16_t((int32_t(left[i]) + int32_t(right[i])) / 2);
			if (std::fwrite(mono, sizeof(int16_t), kSamplesPerLine, stdout) != kSamplesPerLine) {
				std::fclose(input);
				return 1;
			}
		}
	}

	std::fclose(input);
	return 0;
}
