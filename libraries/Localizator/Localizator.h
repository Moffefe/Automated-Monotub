#ifndef Localizator_h
#define Localizator_h

#include <stdint.h>
#include <Arduino.h>   // dla PROGMEM i pgm_read_ptr



enum Language
{
    EN,
    ZHCN,
    ES,
    FR,
    DE,
    PTBR,
    RU,
    JA,
    KO,

    LANG_COUNT              // Keep it always at the end of list
};

enum Phrase
{
    P_OK,
    P_BACK,

    PHRASE_COUNT            // Keep it always at the end of list
};

// Angielski
const char en_ok[]   PROGMEM = "OK";
const char en_back[] PROGMEM = "Back";

// tablica wskaźników
const char* const translations[LANG_COUNT][PHRASE_COUNT] PROGMEM =
{
    // EN
    { en_ok, en_back },
    // ZHCN
    { en_ok, en_back },
    // ES
    { en_ok, en_back },
    // FR
    { en_ok, en_back },
    // DE
    { en_ok, en_back },
    // PTBR
    { en_ok, en_back },
    // RU
    { en_ok, en_back },
    // JA
    { en_ok, en_back },
    // KO
    { en_ok, en_back },
};

class Translator
{
public:
    static void SetLanguage(Language lang)
    {
        currentLang = lang;
    }

    static Language GetLanguage()
    {
        return currentLang;
    }

    static const char* GetPhrase(Phrase phrase)
    {
        return (const char*)pgm_read_ptr(&translations[currentLang][phrase]);
    }
    
    static const char* GetFlag(Language lang)
    {
        switch (lang)
        {
            case EN:    return u8"🇬🇧"; // UK dla EN
            case ZHCN:  return u8"🇨🇳";
            case ES:    return u8"🇪🇸";
            case FR:    return u8"🇫🇷";
            case DE:    return u8"🇩🇪";
            case PTBR:  return u8"🇧🇷";
            case RU:    return u8"🇷🇺";
            case JA:    return u8"🇯🇵";
            case KO:    return u8"🇰🇷";
            default:    return u8"🏳️";
        }
    }

private:
    static Language currentLang;
};

Language Translator::currentLang = EN;

#endif