#pragma once

#include <basis/seadTypes.h>

namespace SphinxRando {

    const char16_t* tryMakeQuestion(const char16_t* originalMessage);
    const char16_t* tryMakeSourceQuestion(const char16_t* originalMessage);
    const char16_t* tryMakeMessage(const char* fileName, const char* key, const char16_t* originalMessage);
    bool tryMakeChoice(const void* choiceNode, const char16_t* originalQuestion,
        const char16_t** outQuestion, const char16_t** outAnswers);
    bool tryMakeChoiceFromAnswers(const void* choiceNode, const void* choiceInfo, const char16_t* originalQuestion,
        const char16_t* const* originalAnswers, s32 answerCount,
        const char16_t** outQuestion, const char16_t** outAnswers);
    s32 tryRemapSelectedChoiceIndex(const void* choiceNode, s32 selectedIndex);
    s32 tryRemapSelectedChoiceInfo(const void* choiceInfo, s32 selectedIndex);
    s32 getLastSelectedAnswerCorrectState();
    void clearLastSelectedAnswerResult();
    const char16_t* tryGetCurrentQuestion();
    void clear();
    s32 getQuestionPoolCount();
    s32 getCurrentQuestionId();

} // namespace SphinxRando
