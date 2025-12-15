#ifndef A_TOUCH_EVENT_H
#define A_TOUCH_EVENT_H

#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>
#include <mutex>

namespace android
{
    class ATouchEvent
    {
    public:
        enum class EventType : uint32_t
        {
            Move,
            TouchDown,
            TouchUp,
        };

        struct TouchEvent
        {
            EventType type;
            int x;
            int y;
            int slot;

            void TransformToScreen(int width, int height, int theta = 0)
            {
                auto k = x, l = width;
                if (90 == theta)
                {
                    x = y;
                    y = transformScalerX - k;
                    width = height;
                    height = l;
                }
                else if (180 == theta)
                {
                    x = transformScalerX - x;
                    y = transformScalerY - y;
                }
                else if (270 == theta)
                {
                    x = transformScalerY - y;
                    y = k;
                    width = height;
                    height = l;
                }

                x = x * width / transformScalerX;
                y = y * height / transformScalerY;
            }
        };

    public:
        ATouchEvent();
        ~ATouchEvent();

        bool GetTouchEvent(TouchEvent* touchEvent);

        static int transformScalerX, transformScalerY;

    private:
        int m_deviceFd;
        std::mutex m_eventMutex;
    };
}

#endif
