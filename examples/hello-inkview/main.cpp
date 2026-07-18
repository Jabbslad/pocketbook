#include "inkview.h"

static int main_handler(int event_type, int param_one, int param_two)
{
    if (event_type == EVT_INIT) {
        ClearScreen();

        ifont *font = OpenFont("LiberationSans", 36, 0);
        SetFont(font, BLACK);

        DrawTextRect(
            0,
            ScreenHeight() / 2 - 30,
            ScreenWidth(),
            60,
            "Hello PocketBook",
            ALIGN_CENTER
        );

        FullUpdate();
        CloseFont(font);
    } else if (event_type == EVT_KEYPRESS) {
        CloseApp();
    }

    return 0;
}

int main()
{
    InkViewMain(main_handler);
    return 0;
}
