#include "pch.h"
#include "CppUnitTest.h"
#include <DeckLinkShortcutSelection.h>
using namespace Microsoft::VisualStudio::CppUnitTestFramework;
namespace Tests
{
    TEST_CLASS(DeckLinkShortcutSelectionTests)
    {
    public:
        TEST_METHOD(IndexedNameIsSelectedExactly)
        {
            const std::vector<std::wstring> names{ L"Magewell (1)", L"DeckLink Duo 2 (1)", L"DeckLink Duo 2 (2)" };
            Assert::AreEqual(1, DeckLinkShortcutIndex(names, 1));
            Assert::AreEqual(2, DeckLinkShortcutIndex(names, 2));
        }
        TEST_METHOD(IndexMatchIsCaseInsensitive)
        {
            const std::vector<std::wstring> names{ L"Player", L"decklink 8K Pro (3)" };
            Assert::AreEqual(1, DeckLinkShortcutIndex(names, 3));
        }
        TEST_METHOD(SingleInputCardWithoutIndexIsSelectedForAnyN)
        {
            const std::vector<std::wstring> names{ L"Player", L"DeckLink Mini Recorder 4K" };
            for (int n = 1; n <= 4; ++n)
                Assert::AreEqual(1, DeckLinkShortcutIndex(names, n));
        }
        TEST_METHOD(FirstUnindexedDeckLinkWins)
        {
            const std::vector<std::wstring> names{ L"Magewell (1)", L"DeckLink Mini Recorder 4K", L"DeckLink Mini Recorder" };
            Assert::AreEqual(1, DeckLinkShortcutIndex(names, 2));
        }
        TEST_METHOD(IndexedCardMissingIndexSelectsNothing)
        {
            const std::vector<std::wstring> names{ L"Player", L"DeckLink Duo 2 (1)", L"DeckLink Duo 2 (2)" };
            Assert::AreEqual(-1, DeckLinkShortcutIndex(names, 3));
        }
        TEST_METHOD(NoDeckLinkSelectsNothing)
        {
            const std::vector<std::wstring> names{ L"Player", L"Magewell (1)" };
            Assert::AreEqual(-1, DeckLinkShortcutIndex(names, 1));
            Assert::AreEqual(-1, DeckLinkShortcutIndex({}, 1));
        }
        TEST_METHOD(IndexIsNotAPrefixMatch)
        {
            const std::vector<std::wstring> names{ L"DeckLink Quad (10)" };
            Assert::AreEqual(-1, DeckLinkShortcutIndex(names, 1));
        }
    };
}
