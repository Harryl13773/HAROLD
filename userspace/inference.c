// Forward pass for the trained 64-16-10 net, using the same Q16.16 integer math export.py verified

#include "libc.h"
#include "model_data.h"

static int forward_and_classify(const int32_t *image)
{
    int32_t hidden[HIDDEN_SIZE];

    for (int h = 0; h < HIDDEN_SIZE; h++)
    {
        int64_t acc = 0;
        for (int i = 0; i < INPUT_SIZE; i++)
        {
            acc += (int64_t)image[i] * (int64_t)weights1[i * HIDDEN_SIZE + h];
        }
        int32_t val = (int32_t)(acc >> FIXED_SHIFT) + bias1[h];
        hidden[h] = val > 0 ? val : 0; // ReLU
    }

    int best_digit = 0;
    int32_t best_score = 0;

    for (int o = 0; o < OUTPUT_SIZE; o++)
    {
        int64_t acc = 0;
        for (int h = 0; h < HIDDEN_SIZE; h++)
        {
            acc += (int64_t)hidden[h] * (int64_t)weights2[h * OUTPUT_SIZE + o];
        }
        int32_t score = (int32_t)(acc >> FIXED_SHIFT) + bias2[o];

        if (o == 0 || score > best_score)
        {
            best_score = score;
            best_digit = o;
        }
    }

    return best_digit;
}

static void append(char *line, int *len, const char *s)
{
    while (*s != '\0')
    {
        line[(*len)++] = *s++;
    }
}

static void append_int(char *line, int *len, int value)
{
    char numbuf[12];
    int nlen = itoa(value, numbuf);
    for (int i = 0; i < nlen; i++)
    {
        line[(*len)++] = numbuf[i];
    }
}

void _start(void)
{
    const char *banner = "HAROLD neural net inference demo (64-16-10, fixed-point)\n";
    write(banner, strlen(banner));

    int correct = 0;

    for (int n = 0; n < NUM_TEST_IMAGES; n++)
    {
        const int32_t *image = &test_images[n * INPUT_SIZE];
        int predicted = forward_and_classify(image);
        int actual = test_labels[n];

        char line[64];
        int len = 0;

        append(line, &len, "Image ");
        append_int(line, &len, n);
        append(line, &len, ": predicted ");
        append_int(line, &len, predicted);
        append(line, &len, ", actual ");
        append_int(line, &len, actual);

        if (predicted == actual)
        {
            append(line, &len, "  correct\n");
            correct++;
        }
        else
        {
            append(line, &len, "  WRONG\n");
        }

        write(line, len);
    }

    char summary[64];
    int len = 0;
    append(summary, &len, "\nResult: ");
    append_int(summary, &len, correct);
    append(summary, &len, " / ");
    append_int(summary, &len, NUM_TEST_IMAGES);
    append(summary, &len, " correct\n");
    write(summary, len);

    exit();

    for (;;)
    {
    }
}