#pragma once
#ifdef _3DS
namespace ProfileHintLayout3DS
{
inline bool
Matches(const wchar *text, const char *ascii)
{
	while(*ascii && *text == (unsigned char)*ascii) {
		++text;
		++ascii;
	}
	return *ascii == 0 && *text == 0;
}

inline bool
Draw(wchar *text, float left, float top, float right, float padding, float scaleX, float scaleY, float alpha)
{
	static const char *const hints[] = {"Flat Quality",
	                                    "Flat Performance",
	                                    "Stereo Quality~n~Normal View",
	                                    "Stereo Quality~n~Extended Depth",
	                                    "Stereo Performance~n~Normal View",
	                                    "Stereo Performance~n~Extended Depth"};
	bool matched = false;
	for(const char *hint : hints)
		if(Matches(text, hint)) {
			matched = true;
			break;
		}
	if(!matched) return false; // Keep ordinary help/mission text layout unchanged.

	CFont::SetScale(scaleX, scaleY);
	wchar lines[2][80] = {};
	unsigned count = 0, row = 0;
	float width = 0.0f;
	for(const wchar *p = text;; ++p) {
		const bool newline = p[0] == '~' && p[1] == 'n' && p[2] == '~';
		if(!*p || newline) {
			lines[row][count] = 0;
			const float lineWidth = CFont::GetStringWidth(lines[row], true);
			if(lineWidth > width) width = lineWidth;
			count = 0;
			if(!*p) break;
			++row;
			p += 2;
		} else
			lines[row][count++] = *p;
	}
#ifdef MORE_LANGUAGES
	const float wrapInset = CFont::IsJapaneseFont() ? SCREEN_SCALE_X(42.0f) : 0.0f;
#else
	const float wrapInset = 0.0f;
#endif
	const float available = right - left - padding - wrapInset;
	if(width > available) {
		CFont::SetScale(scaleX * available / width, scaleY);
		width = available;
	}
	CFont::SetWrapx(left + width + padding + wrapInset);
	// Draw complete lines separately. LCS's ordinary font path ignores ~n~;
	// feeding the combined string to PrintString can still split the preset.
	CSprite2d::DrawRect(CRect(left - padding * 0.5f, top - 2.0f * scaleY, left + width + padding * 0.5f, top + (row * 18.0f + 22.0f) * scaleY),
	                    CRGBA(0, 0, 0, alpha * 0.9f));
	CFont::SetBackgroundOff();
	for(unsigned i = 0; i <= row; ++i) CFont::PrintString(left, top + i * 18.0f * scaleY, lines[i]);
	CFont::SetBackgroundOn();
	return true;
}
} // namespace ProfileHintLayout3DS
#endif
