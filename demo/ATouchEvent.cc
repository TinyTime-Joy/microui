#include "ATouchEvent.h"
#include "Global.h"

#include <dirent.h>
#include <cstring>
#include <vector>
#include <sys/ioctl.h>

namespace android
{
    int ATouchEvent::transformScalerX = -1;
    int ATouchEvent::transformScalerY = -1;

    static int CountEventDevices()
    {
        DIR* dir = opendir("/dev/input/");
        if (!dir) return 0;

        int count = 0;
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr)
        {
            if (strstr(entry->d_name, "event"))
                count++;
        }
        closedir(dir);
        return count;
    }

    static int DetectTouchDevice()
    {
        int deviceCount = CountEventDevices();
        if (deviceCount == 0)
        {
            LogDebug("No input devices found");
            return -1;
        }

        LogInfo("Found %d input devices. Detecting touch...", deviceCount);

        std::vector<int> fdList;
        fdList.reserve(deviceCount);

        for (int i = 0; i < deviceCount; i++)
        {
            char path[64];
            sprintf(path, "/dev/input/event%d", i);
            fdList.push_back(open(path, O_RDONLY | O_NONBLOCK));
        }

        struct input_event event;

        while (true)
        {
            for (int i = 0; i < deviceCount; i++)
            {
                if (fdList[i] < 0) continue;

                for (int j = 0; j < 8; j++)
                {
                    memset(&event, 0, sizeof(event));
                    if (read(fdList[i], &event, sizeof(event)) != sizeof(event))
                        break;

                    if (event.type == EV_ABS &&
                        (event.code == ABS_MT_POSITION_X || event.code == ABS_MT_POSITION_Y ||
                         event.code == ABS_X || event.code == ABS_Y))
                    {
                        LogInfo("Touch device: /dev/input/event%d", i);
                        for (int k = 0; k < deviceCount; k++)
                            if (fdList[k] >= 0) close(fdList[k]);
                        return i;
                    }
                }
            }
            usleep(3000);
        }
        return -1;
    }

    static bool GetTouchResolution(int fd, int& maxX, int& maxY)
    {
        struct input_absinfo absInfo;
        if (ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &absInfo) == 0)
        {
            maxX = absInfo.maximum;
            if (ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), &absInfo) == 0)
            {
                maxY = absInfo.maximum;
                LogInfo("Touch resolution: %d x %d", maxX, maxY);
                return true;
            }
        }
        return false;
    }

    ATouchEvent::ATouchEvent() : m_deviceFd(-1)
    {
        int id = DetectTouchDevice();
        if (id < 0) return;

        char path[64];
        sprintf(path, "/dev/input/event%d", id);
        m_deviceFd = open(path, O_RDONLY | O_NONBLOCK);

        if (m_deviceFd < 0)
        {
            LogError("Failed to open %s", path);
            return;
        }

        if (!GetTouchResolution(m_deviceFd, transformScalerX, transformScalerY))
        {
            LogError("Failed to get touch resolution");
            close(m_deviceFd);
            m_deviceFd = -1;
            return;
        }

        LogInfo("ATouchEvent initialized");
    }

    ATouchEvent::~ATouchEvent()
    {
        if (m_deviceFd >= 0)
            close(m_deviceFd);
    }

    bool ATouchEvent::GetTouchEvent(TouchEvent* touchEvent)
    {
        std::lock_guard<std::mutex> lock(m_eventMutex);

        static std::vector<input_event> eventQueue;
        static int lastX = 0, lastY = 0;
        static int currentSlot = 0;

        if (m_deviceFd < 0 || !touchEvent)
            return false;

        struct input_event ev;
        if (read(m_deviceFd, &ev, sizeof(ev)) != sizeof(ev))
            return false;

        if (ev.type != EV_SYN || ev.code != SYN_REPORT)
        {
            eventQueue.push_back(ev);
            return false;
        }

        if (eventQueue.empty())
            return false;

        touchEvent->type = EventType::Move;
        touchEvent->x = lastX;
        touchEvent->y = lastY;
        touchEvent->slot = currentSlot;

        for (const auto& e : eventQueue)
        {
            if (e.type == EV_ABS)
            {
                if (e.code == ABS_MT_SLOT)
                {
                    currentSlot = e.value;
                    touchEvent->slot = currentSlot;
                }
                if (e.code == ABS_MT_TRACKING_ID)
                {
                    touchEvent->type = (e.value == -1) ? EventType::TouchUp : EventType::TouchDown;
                }
                if (e.code == ABS_MT_POSITION_X || e.code == ABS_X)
                {
                    touchEvent->x = e.value;
                    lastX = e.value;
                }
                if (e.code == ABS_MT_POSITION_Y || e.code == ABS_Y)
                {
                    touchEvent->y = e.value;
                    lastY = e.value;
                }
            }
        }

        eventQueue.clear();
        return true;
    }

}
