#pragma once

#include <cmath>
#include <cassert>
#include <iostream>
#include <string>
#include <vector>
#include <array>
#include <memory>
#include <unordered_set>
#include <unordered_map>
#include <map>
#include "imgui.h"

class IMGUI_API TextEditor
{
public:
	// ------------- Exposed API ------------- //

	TextEditor();
	~TextEditor();

	enum class PaletteId
	{
		Dark, Light, Mariana, RetroBlue
	};
	enum class LanguageDefinitionId
	{
		None, Cpp, C, Cs, Python, Lua, Json, Sql, AngelScript, Glsl, Hlsl,
		TasNotation,	// flycast-dojo: the TAS input-notation language
		Ppad			// flycast-dojo: the PPAD / V PRO program language (steps with the hold baked in)
	};
	enum class SetViewAtLineMode
	{
		FirstVisibleLine, Centered, LastVisibleLine
	};
	// flycast-dojo: public so callers can retint entries (player-colored buttons)
	enum class PaletteIndex
	{
		Default,
		Keyword,
		Number,
		String,
		CharLiteral,
		Punctuation,
		Preprocessor,
		Identifier,
		KnownIdentifier,
		PreprocIdentifier,
		Comment,
		MultiLineComment,
		Background,
		Cursor,
		Selection,
		ErrorMarker,
		ControlCharacter,
		Breakpoint,
		LineNumber,
		CurrentLineFill,
		CurrentLineFillInactive,
		CurrentLineEdge,
		Max
	};

	inline void SetReadOnlyEnabled(bool aValue) { mReadOnly = aValue; }
	inline bool IsReadOnlyEnabled() const { return mReadOnly; }
	inline void SetAutoIndentEnabled(bool aValue) { mAutoIndent = aValue; }
	inline bool IsAutoIndentEnabled() const { return mAutoIndent; }
	inline void SetShowWhitespacesEnabled(bool aValue) { mShowWhitespaces = aValue; }
	inline bool IsShowWhitespacesEnabled() const { return mShowWhitespaces; }
	inline void SetShowLineNumbersEnabled(bool aValue) { mShowLineNumbers = aValue; }
	inline bool IsShowLineNumbersEnabled() const { return mShowLineNumbers; }
	// flycast-dojo: first gutter number (default 1; the TAS notepad sets 0 so the gutter matches the
	// 0-indexed piano roll / timeline / glyph view instead of the editor's usual 1-based line numbers)
	inline void SetLineNumberStart(int aBase) { mLineNumberBase = aBase; }
	inline void SetLeftMargin(int aPixels) { mLeftMargin = aPixels < 0 ? 0 : aPixels; }	// flycast-dojo: the gutter's left air
	// flycast-dojo: the gutter text (line numbers / labels) drawn at a fraction of the text size (dev: "only target the gutter")
	inline void SetGutterScale(float aScale) { mGutterScale = aScale < 0.4f ? 0.4f : (aScale > 1.5f ? 1.5f : aScale); }
	inline void SetShortTabsEnabled(bool aValue) { mShortTabs = aValue; }
	inline bool IsShortTabsEnabled() const { return mShortTabs; }
	inline int GetLineCount() const { return mLines.size(); }
	int GetSelectedLineCount() const;	// flycast-dojo: the wait-counting readout
	void SetPalette(PaletteId aValue);
	// flycast-dojo: override one palette entry (format 0xRRGGBBAA, like the
	// built-in palettes) - applied to the ACTIVE palette, so call after
	// SetPalette; the notepad re-applies per frame
	inline void SetPaletteColor(PaletteIndex aIndex, ImU32 aColor) { mPalette[(int)aIndex] = aColor; }
	inline ImU32 GetPaletteColor(PaletteIndex aIndex) const { return mPalette[(int)aIndex]; }	// flycast-dojo
	PaletteId GetPalette() const { return mPaletteId; }
	void SetLanguageDefinition(LanguageDefinitionId aValue);
	LanguageDefinitionId GetLanguageDefinition() const { return mLanguageDefinitionId; };
	const char* GetLanguageDefinitionName() const;
	void SetTabSize(int aValue);
	inline int GetTabSize() const { return mTabSize; }
	void SetLineSpacing(float aValue);
	inline float GetLineSpacing() const { return mLineSpacing;  }
	// flycast-dojo: per-editor text zoom (Ctrl+wheel / Ctrl+= / Ctrl+-)
	inline void SetFontScale(float aValue) { mFontScale = aValue < 0.5f ? 0.5f : (aValue > 3.f ? 3.f : aValue); }
	inline float GetFontScale() const { return mFontScale; }

	inline static void SetDefaultPalette(PaletteId aValue) { defaultPalette = aValue; }
	inline static PaletteId GetDefaultPalette() { return defaultPalette; }

	void SelectAll();
	void SelectLine(int aLine);
	void SelectRegion(int aStartLine, int aStartChar, int aEndLine, int aEndChar);
	void SelectNextOccurrenceOf(const char* aText, int aTextSize, bool aCaseSensitive = true);
	void SelectAllOccurrencesOf(const char* aText, int aTextSize, bool aCaseSensitive = true);
	void SelectPreviousOccurrenceOf(const char* aText, int aTextSize, bool aCaseSensitive = true);	// flycast-dojo: find-previous
	bool AnyCursorHasSelection() const;
	bool AllCursorsHaveSelection() const;
	void ClearExtraCursors();
	// flycast-dojo: arrow-free multicursor access (buttons; GPU drivers eat
	// Ctrl+Alt+Arrow on many Windows boxes)
	inline void AddCursorAbove() { AddCursorAboveOrBelow(false); }
	inline void AddCursorBelow() { AddCursorAboveOrBelow(true); }
	void AppendLines(const std::string& aText);	// flycast-dojo: undo-friendly append
	void ClearSelections();
	void SetCursorPosition(int aLine, int aCharIndex);
	inline void GetCursorPosition(int& outLine, int& outColumn) const
	{
		auto coords = GetSanitizedCursorCoordinates();
		outLine = coords.mLine;
		outColumn = coords.mColumn;
	}
	int GetFirstVisibleLine();
	int GetLastVisibleLine();
	void SetViewAtLine(int aLine, SetViewAtLineMode aMode);
	void RequestFocus() { mRequestFocus = true; }	// flycast-dojo: Tab panel-swap - grab focus next Render
	// flycast-dojo: paint one line's full-row background (the notepad's active-frame / playhead highlight); -1 = off
	inline void SetHighlightLine(int aLine, ImU32 aColor) { mHighlightLine = aLine; mHighlightColor = aColor; }
	// flycast-dojo: diagnostics - VS Code-style wavy underlines under a char range, the message on hover (the TAS
	// notepad's per-token lint). mStart/mEnd are CHAR INDICES into the line (not columns); mColor is IM_COL32.
	enum class DecoStyle { Squiggle, Bar, None };	// flycast-dojo: how a Diagnostic draws (None = hover message only)
	struct Diagnostic { int mLine = 0; int mStart = 0; int mEnd = 0; ImU32 mColor = 0; std::string mMessage; DecoStyle mStyle = DecoStyle::Squiggle; };
	inline void SetDiagnostics(std::vector<Diagnostic> aDiags) { mDiagnostics = std::move(aDiags); }
	inline void ClearDiagnostics() { mDiagnostics.clear(); }
	inline int GetDiagnosticCount() const { return (int)mDiagnostics.size(); }
	// flycast-dojo: decorations - the same shape as diagnostics, kept as a second list (the notepad's PPAD step
	// badges: a hover message per step, a gold bar under the playhead's step). Both lists draw and feed the tooltip.
	inline void SetDecorations(std::vector<Diagnostic> aDecos) { mDecorations = std::move(aDecos); }
	inline void ClearDecorations() { mDecorations.clear(); }
	// flycast-dojo: faux bold per palette slot - the editor has one font, so a bold glyph is drawn twice, 1px apart
	// (the terminal trick). The notepad bolds the PPAD step separator '/' (Punctuation) this way.
	inline void SetBoldPalette(PaletteIndex aIndex, bool aBold) { if (aBold) mBoldMask |= 1u << (int)aIndex; else mBoldMask &= ~(1u << (int)aIndex); }
	// flycast-dojo: custom gutter labels (the notepad's PPAD "<1-10>  f 134" step-range / frame-offset gutter). One
	// entry per line; an empty text falls back to the line number; the gutter widens to the longest label (monospace).
	// mColor 0 = the LineNumber palette color.
	struct GutterLabel { std::string mText; ImU32 mColor = 0; };
	void SetGutterLabels(std::vector<GutterLabel> aLabels);
	inline void ClearGutterLabels() { mGutterLabels.clear(); mGutterMaxLen = 0; }

	void Copy();
	void Cut();
	void Paste();
	void Undo(int aSteps = 1);
	void Redo(int aSteps = 1);
	inline bool CanUndo() const { return !mReadOnly && mUndoIndex > 0; };
	inline bool CanRedo() const { return !mReadOnly && mUndoIndex < (int)mUndoBuffer.size(); };
	inline int GetUndoIndex() const { return mUndoIndex; };

	void SetText(const std::string& aText);
	std::string GetText() const;

	void SetTextLines(const std::vector<std::string>& aLines);
	std::vector<std::string> GetTextLines() const;

	bool Render(const char* aTitle, bool aParentIsFocused = false, const ImVec2& aSize = ImVec2(), bool aBorder = false);

	void ImGuiDebugPanel(const std::string& panelName = "Debug");
	void UnitTests();

private:
	// ------------- Generic utils ------------- //

	static inline ImVec4 U32ColorToVec4(ImU32 in)
	{
		float s = 1.0f / 255.0f;
		return ImVec4(
			((in >> IM_COL32_A_SHIFT) & 0xFF) * s,
			((in >> IM_COL32_B_SHIFT) & 0xFF) * s,
			((in >> IM_COL32_G_SHIFT) & 0xFF) * s,
			((in >> IM_COL32_R_SHIFT) & 0xFF) * s);
	}
	static inline bool IsUTFSequence(char c)
	{
		return (c & 0xC0) == 0x80;
	}
	static inline float Distance(const ImVec2& a, const ImVec2& b)
	{
		float x = a.x - b.x;
		float y = a.y - b.y;
		return sqrt(x * x + y * y);
	}
	template<typename T>
	static inline T Max(T a, T b) { return a > b ? a : b; }
	template<typename T>
	static inline T Min(T a, T b) { return a < b ? a : b; }

	// ------------- Internal ------------- //


	// Represents a character coordinate from the user's point of view,
	// i. e. consider an uniform grid (assuming fixed-width font) on the
	// screen as it is rendered, and each cell has its own coordinate, starting from 0.
	// Tabs are counted as [1..mTabSize] count empty spaces, depending on
	// how many space is necessary to reach the next tab stop.
	// For example, coordinate (1, 5) represents the character 'B' in a line "\tABC", when mTabSize = 4,
	// because it is rendered as "    ABC" on the screen.
	struct Coordinates
	{
		int mLine, mColumn;
		Coordinates() : mLine(0), mColumn(0) {}
		Coordinates(int aLine, int aColumn) : mLine(aLine), mColumn(aColumn)
		{
			assert(aLine >= 0);
			assert(aColumn >= 0);
		}
		static Coordinates Invalid() { static Coordinates invalid(-1, -1); return invalid; }

		bool operator ==(const Coordinates& o) const
		{
			return
				mLine == o.mLine &&
				mColumn == o.mColumn;
		}

		bool operator !=(const Coordinates& o) const
		{
			return
				mLine != o.mLine ||
				mColumn != o.mColumn;
		}

		bool operator <(const Coordinates& o) const
		{
			if (mLine != o.mLine)
				return mLine < o.mLine;
			return mColumn < o.mColumn;
		}

		bool operator >(const Coordinates& o) const
		{
			if (mLine != o.mLine)
				return mLine > o.mLine;
			return mColumn > o.mColumn;
		}

		bool operator <=(const Coordinates& o) const
		{
			if (mLine != o.mLine)
				return mLine < o.mLine;
			return mColumn <= o.mColumn;
		}

		bool operator >=(const Coordinates& o) const
		{
			if (mLine != o.mLine)
				return mLine > o.mLine;
			return mColumn >= o.mColumn;
		}

		Coordinates operator -(const Coordinates& o)
		{
			return Coordinates(mLine - o.mLine, mColumn - o.mColumn);
		}

		Coordinates operator +(const Coordinates& o)
		{
			return Coordinates(mLine + o.mLine, mColumn + o.mColumn);
		}
	};

	struct Cursor
	{
		Coordinates mInteractiveStart = { 0, 0 };
		Coordinates mInteractiveEnd = { 0, 0 };
		inline Coordinates GetSelectionStart() const { return mInteractiveStart < mInteractiveEnd ? mInteractiveStart : mInteractiveEnd; }
		inline Coordinates GetSelectionEnd() const { return mInteractiveStart > mInteractiveEnd ? mInteractiveStart : mInteractiveEnd; }
		inline bool HasSelection() const { return mInteractiveStart != mInteractiveEnd; }
	};

	struct EditorState // state to be restored with undo/redo
	{
		int mCurrentCursor = 0;
		int mLastAddedCursor = 0;
		std::vector<Cursor> mCursors = { {{0,0}} };
		void AddCursor();
		int GetLastAddedCursorIndex();
		void SortCursorsFromTopToBottom();
	};

	struct Identifier
	{
		Coordinates mLocation;
		std::string mDeclaration;
	};

	typedef std::unordered_map<std::string, Identifier> Identifiers;
	typedef std::array<ImU32, (unsigned)PaletteIndex::Max> Palette;

	struct Glyph
	{
		char mChar;
		PaletteIndex mColorIndex = PaletteIndex::Default;
		bool mComment : 1;
		bool mMultiLineComment : 1;
		bool mPreprocessor : 1;

		Glyph(char aChar, PaletteIndex aColorIndex) : mChar(aChar), mColorIndex(aColorIndex),
			mComment(false), mMultiLineComment(false), mPreprocessor(false) {}
	};

	typedef std::vector<Glyph> Line;

	struct LanguageDefinition
	{
		typedef std::pair<std::string, PaletteIndex> TokenRegexString;
		typedef bool(*TokenizeCallback)(const char* in_begin, const char* in_end, const char*& out_begin, const char*& out_end, PaletteIndex& paletteIndex);

		std::string mName;
		std::unordered_set<std::string> mKeywords;
		Identifiers mIdentifiers;
		Identifiers mPreprocIdentifiers;
		std::string mCommentStart, mCommentEnd, mSingleLineComment;
		char mPreprocChar = '#';
		TokenizeCallback mTokenize = nullptr;
		std::vector<TokenRegexString> mTokenRegexStrings;
		bool mCaseSensitive = true;

		static const LanguageDefinition& Cpp();
		static const LanguageDefinition& Hlsl();
		static const LanguageDefinition& Glsl();
		static const LanguageDefinition& Python();
		static const LanguageDefinition& C();
		static const LanguageDefinition& Sql();
		static const LanguageDefinition& AngelScript();
		static const LanguageDefinition& Lua();
		static const LanguageDefinition& Cs();
		static const LanguageDefinition& Json();
		static const LanguageDefinition& TasNotation();	// flycast-dojo
		static const LanguageDefinition& Ppad();		// flycast-dojo
	};

	enum class UndoOperationType { Add, Delete };
	struct UndoOperation
	{
		std::string mText;
		TextEditor::Coordinates mStart;
		TextEditor::Coordinates mEnd;
		UndoOperationType mType;
	};


	class UndoRecord
	{
	public:
		UndoRecord() {}
		~UndoRecord() {}

		UndoRecord(
			const std::vector<UndoOperation>& aOperations,
			TextEditor::EditorState& aBefore,
			TextEditor::EditorState& aAfter);

		void Undo(TextEditor* aEditor);
		void Redo(TextEditor* aEditor);

		std::vector<UndoOperation> mOperations;

		EditorState mBefore;
		EditorState mAfter;
	};

	std::string GetText(const Coordinates& aStart, const Coordinates& aEnd) const;
	std::string GetClipboardText() const;
	std::string GetSelectedText(int aCursor = -1) const;

	void SetCursorPosition(const Coordinates& aPosition, int aCursor = -1, bool aClearSelection = true);

	int InsertTextAt(Coordinates& aWhere, const char* aValue);
	void InsertTextAtCursor(const char* aValue, int aCursor = -1);

	enum class MoveDirection { Right = 0, Left = 1, Up = 2, Down = 3 };
	bool Move(int& aLine, int& aCharIndex, bool aLeft = false, bool aLockLine = false) const;
	void MoveCharIndexAndColumn(int aLine, int& aCharIndex, int& aColumn) const;
	void MoveCoords(Coordinates& aCoords, MoveDirection aDirection, bool aWordMode = false, int aLineCount = 1) const;

	void MoveUp(int aAmount = 1, bool aSelect = false);
	void MoveDown(int aAmount = 1, bool aSelect = false);
	void MoveLeft(bool aSelect = false, bool aWordMode = false);
	void MoveRight(bool aSelect = false, bool aWordMode = false);
	void MoveTop(bool aSelect = false);
	void MoveBottom(bool aSelect = false);
	void MoveHome(bool aSelect = false);
	void MoveEnd(bool aSelect = false);
	void EnterCharacter(ImWchar aChar, bool aShift);
	void Backspace(bool aWordMode = false);
	void Delete(bool aWordMode = false, const EditorState* aEditorState = nullptr);

	void SetSelection(Coordinates aStart, Coordinates aEnd, int aCursor = -1);
	void SetSelection(int aStartLine, int aStartChar, int aEndLine, int aEndChar, int aCursor = -1);

	void SelectNextOccurrenceOf(const char* aText, int aTextSize, int aCursor = -1, bool aCaseSensitive = true);
	void AddCursorForNextOccurrence(bool aCaseSensitive = true);
	bool FindNextOccurrence(const char* aText, int aTextSize, const Coordinates& aFrom, Coordinates& outStart, Coordinates& outEnd, bool aCaseSensitive = true);
	void SelectPreviousOccurrenceOf(const char* aText, int aTextSize, int aCursor = -1, bool aCaseSensitive = true);	// flycast-dojo
	bool FindPreviousOccurrence(const char* aText, int aTextSize, const Coordinates& aFrom, Coordinates& outStart, Coordinates& outEnd, bool aCaseSensitive = true);	// flycast-dojo
	bool FindMatchingBracket(int aLine, int aCharIndex, Coordinates& out);
	void ChangeCurrentLinesIndentation(bool aIncrease);
	void MoveUpCurrentLines();
	void MoveDownCurrentLines();
	void DuplicateCurrentLines(bool aMoveCursorDown);	// flycast-dojo: VSCode Shift+Alt+Up/Down
	void AddCursorAboveOrBelow(bool aBelow);			// flycast-dojo: VSCode Ctrl+Alt+Up/Down
	void ToggleLineComment();
	void RemoveCurrentLines();

	float TextDistanceToLineStart(const Coordinates& aFrom, bool aSanitizeCoords = true) const;
	void EnsureCursorVisible(int aCursor = -1, bool aStartToo = false);

	Coordinates SanitizeCoordinates(const Coordinates& aValue) const;
	Coordinates GetSanitizedCursorCoordinates(int aCursor = -1, bool aStart = false) const;
	Coordinates ScreenPosToCoordinates(const ImVec2& aPosition, bool* isOverLineNumber = nullptr) const;
	Coordinates FindWordStart(const Coordinates& aFrom) const;
	Coordinates FindWordEnd(const Coordinates& aFrom) const;
	int GetCharacterIndexL(const Coordinates& aCoordinates) const;
	int GetCharacterIndexR(const Coordinates& aCoordinates) const;
	int GetCharacterColumn(int aLine, int aIndex) const;
	int GetFirstVisibleCharacterIndex(int aLine) const;
	int GetLineMaxColumn(int aLine, int aLimit = -1) const;

	Line& InsertLine(int aIndex);
	void RemoveLine(int aIndex, const std::unordered_set<int>* aHandledCursors = nullptr);
	void RemoveLines(int aStart, int aEnd);
	void DeleteRange(const Coordinates& aStart, const Coordinates& aEnd);
	void DeleteSelection(int aCursor = -1);

	void RemoveGlyphsFromLine(int aLine, int aStartChar, int aEndChar = -1);
	void AddGlyphsToLine(int aLine, int aTargetIndex, Line::iterator aSourceStart, Line::iterator aSourceEnd);
	void AddGlyphToLine(int aLine, int aTargetIndex, Glyph aGlyph);
	ImU32 GetGlyphColor(const Glyph& aGlyph) const;

	void HandleKeyboardInputs(bool aParentIsFocused = false);
	void HandleMouseInputs();
	void UpdateViewVariables(float aScrollX, float aScrollY);
	void Render(bool aParentIsFocused = false);

	void OnCursorPositionChanged();
	void OnLineChanged(bool aBeforeChange, int aLine, int aColumn, int aCharCount, bool aDeleted);
	void MergeCursorsIfPossible();

	void AddUndo(UndoRecord& aValue);

	void Colorize(int aFromLine = 0, int aCount = -1);
	void ColorizeRange(int aFromLine = 0, int aToLine = 0);
	void ColorizeInternal();

	std::vector<Line> mLines;
	EditorState mState;
	std::vector<UndoRecord> mUndoBuffer;
	int mUndoIndex = 0;

	int mTabSize = 4;
	float mLineSpacing = 1.0f;
	float mFontScale = 1.0f;	// flycast-dojo
	bool mReadOnly = false;
	bool mRequestFocus = false;	// flycast-dojo
	bool mAutoIndent = true;
	bool mShowWhitespaces = true;
	bool mShowLineNumbers = true;
	int mLineNumberBase = 1;	// flycast-dojo: gutter numbering origin (see SetLineNumberStart)
	bool mShortTabs = false;

	int mSetViewAtLine = -1;
	SetViewAtLineMode mSetViewAtLineMode;
	int mEnsureCursorVisible = -1;
	bool mEnsureCursorVisibleStartToo = false;
	bool mScrollToTop = false;
	int mHighlightLine = -1;	// flycast-dojo: full-row highlight (active movie frame / playhead); -1 = none
	ImU32 mHighlightColor = 0;
	std::vector<Diagnostic> mDiagnostics;	// flycast-dojo: wavy-underline diagnostics (SetDiagnostics)
	std::vector<Diagnostic> mDecorations;	// flycast-dojo: step badges etc. (SetDecorations)
	unsigned mBoldMask = 0;					// flycast-dojo: palette slots drawn faux-bold (SetBoldPalette)
	std::vector<GutterLabel> mGutterLabels;	// flycast-dojo: per-line gutter labels (SetGutterLabels)
	int mGutterMaxLen = 0;					// flycast-dojo: longest label, in characters (gutter width)
	float mGutterScale = 1.0f;				// flycast-dojo: gutter text size relative to the text (SetGutterScale)

	float mTextStart = 20.0f; // position (in pixels) where a code line starts relative to the left of the TextEditor.
	int mLeftMargin = 10;
	ImVec2 mCharAdvance;
	float mCurrentSpaceHeight = 20.0f;
	float mCurrentSpaceWidth = 20.0f;
	float mLastClickTime = -1.0f;
	ImVec2 mLastClickPos;
	int mFirstVisibleLine = 0;
	int mLastVisibleLine = 0;
	int mVisibleLineCount = 0;
	int mFirstVisibleColumn = 0;
	int mLastVisibleColumn = 0;
	int mVisibleColumnCount = 0;
	float mContentWidth = 0.0f;
	float mContentHeight = 0.0f;
	float mScrollX = 0.0f;
	float mScrollY = 0.0f;
	bool mPanning = false;
	bool mDraggingSelection = false;
	ImVec2 mLastMousePos;
	bool mCursorPositionChanged = false;
	bool mCursorOnBracket = false;
	Coordinates mMatchingBracketCoords;

	int mColorRangeMin = 0;
	int mColorRangeMax = 0;
	bool mCheckComments = true;
	PaletteId mPaletteId;
	Palette mPalette;
	LanguageDefinitionId mLanguageDefinitionId;
	const LanguageDefinition* mLanguageDefinition = nullptr;

	inline bool IsHorizontalScrollbarVisible() const { return mCurrentSpaceWidth > mContentWidth; }
	inline bool IsVerticalScrollbarVisible() const { return mCurrentSpaceHeight > mContentHeight; }
	inline int TabSizeAtColumn(int aColumn) const { return mTabSize - (aColumn % mTabSize); }

	static const Palette& GetDarkPalette();
	static const Palette& GetMarianaPalette();
	static const Palette& GetLightPalette();
	static const Palette& GetRetroBluePalette();
	static const std::unordered_map<char, char> OPEN_TO_CLOSE_CHAR;
	static const std::unordered_map<char, char> CLOSE_TO_OPEN_CHAR;
	static PaletteId defaultPalette;

private:
    struct RegexList;
    std::shared_ptr<RegexList> mRegexList;
};
