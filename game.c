#include "game.h"
#include <string.h>
#include <stdlib.h>

#define LETTER_BIT(letter) ((uint32_t)1u << ((letter) - 'a'))
letter_set_t letter_set_empty()
{
    letter_set_t result = {
        .bitfield = 0,
    };
    return result;
}

letter_set_t letter_set_full()
{
    letter_set_t result = {
        .bitfield = ((uint32_t)1u << 26) - 1u,
    };
    return result;
}

char normalize(char letter)
{
    if ('A' <= letter && letter <= 'Z') return letter - 'A' + 'a';
    return letter;
}

bool is_letter(char character)
{
    unsigned char uc = (unsigned char)character;
    return 'a' <= uc && uc <= 'z';
}

bool letter_set_contains(letter_set_t set, char letter)
{
    letter = normalize(letter);
    if (!is_letter(letter)) return false;
    return LETTER_BIT(letter) & set.bitfield;
}

letter_set_t letter_set_add(letter_set_t set, char letter)
{
    letter = normalize(letter);
    if (!is_letter(letter)) return set;

    letter_set_t result = {
        .bitfield = set.bitfield | LETTER_BIT(letter)
    };
    return result;
}

letter_set_t letter_set_remove(letter_set_t set, char letter)
{
    letter = normalize(letter);
    if (!is_letter(letter)) return set;

    letter_set_t result = {
        .bitfield = set.bitfield & ~LETTER_BIT(letter)
    };
    return result;
}

letter_set_t letter_set_intersect(letter_set_t left, letter_set_t right)
{
    letter_set_t result = {
        .bitfield = left.bitfield & right.bitfield
    };
    return result;
}

letter_set_t letter_set_union(letter_set_t left, letter_set_t right)
{
    letter_set_t result = {
        .bitfield = left.bitfield | right.bitfield
    };
    return result;
}

size_t letter_set_size(letter_set_t set)
{
    size_t size = 0;
    for (size_t i = 0; i < 26; i++)
    {
        if ((1u << i) & set.bitfield) size++;
    }

    return size;
}

bool letter_set_is_empty(letter_set_t set) { return set.bitfield == 0; }

bool letter_set_equals(letter_set_t left, letter_set_t right) { return left.bitfield == right.bitfield; }

bool secret_word_init_from_c_string(secret_word_t *word, const char *string)
{
    return secret_word_init_from_buffer_and_size(word, string, strlen(string));
}

bool secret_word_init_from_buffer_and_size(secret_word_t *word, const char *buffer, size_t size)
{
    if (size == 0) return false;
    for (size_t i = 0; i < size; i++)
    {
        if (!is_letter(normalize(buffer[i]))) return false;
    }

    word->word = (char*) malloc((size + 1) * sizeof(char));
    if (word->word == NULL) return false;
    word->word_length = size;
    memcpy(word->word, buffer, size * sizeof(char));
    word->word[size] = '\0'; /* null-terminate for safety */
    for (size_t i = 0; i < size; i++)
    {
        word->word[i] = normalize(word->word[i]);
    }

    word->revealed_bitfield_length = size / 8 + (size % 8 == 0 ? 0 : 1);
    word->revealed_bitfield = (uint8_t*) calloc(word->revealed_bitfield_length, sizeof(uint8_t));
    if (word->revealed_bitfield == NULL)
    {
         free(word->word);
        word->word = NULL;
        word->word_length = 0;
        return false;
    }

    word->incorrect_guesses = letter_set_empty();
    word->all_guesses = letter_set_empty();

    return true;
}

void secret_word_free(secret_word_t *word)
{
    free(word->word);
    word->word = NULL;
    free(word->revealed_bitfield);
    word->revealed_bitfield = NULL;
    word->word_length = 0;
    word->revealed_bitfield_length = 0;
}

secret_word_guess_result_t secret_word_guess(secret_word_t *word, char guess)
{
    guess = normalize(guess);
    if (!is_letter(guess)) return SECRET_WORD_GUESS_INVALID_LETTER;
    if (letter_set_contains(word->all_guesses, guess)) return SECRET_WORD_GUESS_ALREADY_GUESSED_LETTER;

    bool found = false;

    for (size_t i = 0; i < word->word_length; i++)
    {
        if (word->word[i] == guess)
        {
            found = true;
            word->revealed_bitfield[i / 8] = word->revealed_bitfield[i / 8] | (1 << (i % 8));
        }
    }

    word->all_guesses = letter_set_add(word->all_guesses, guess);
    if (found)
    {
        return SECRET_WORD_GUESS_CORRECT;
    }
    else
    {
        word->incorrect_guesses = letter_set_add(word->incorrect_guesses, guess);
        return SECRET_WORD_GUESS_INCORRECT;
    }
}

secret_word_letter_at_result_t secret_word_letter_at(const secret_word_t *word, size_t index, char *result)
{
    if (index >= word->word_length) return SECRET_WORD_LETTER_INDEX_OUT_OF_BOUNDS;

    if (word->revealed_bitfield[index / 8] & (1 << (index % 8)))
    {
        if (result != NULL) *result = word->word[index];
        return SECRET_WORD_LETTER_REVEALED;
    }
    else return SECRET_WORD_LETTER_HIDDEN;
}

size_t secret_word_incorrect_guess_count(const secret_word_t *word)
{
    return letter_set_size(word->incorrect_guesses);
}

bool secret_word_is_solved(const secret_word_t* word)
{
    for (size_t i = 0; i < word->word_length; i++)
    {
        if (secret_word_letter_at(word, i, NULL) != SECRET_WORD_LETTER_REVEALED) return false;
    }

    return true;
}