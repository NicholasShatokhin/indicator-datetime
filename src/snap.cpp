/*
 * Copyright 2014 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3, as published
 * by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranties of
 * MERCHANTABILITY, SATISFACTORY QUALITY, or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors:
 *   Charles Kerr <charles.kerr@canonical.com>
 */

#include <datetime/snap.h>
#include <datetime/utils.h> // is_locale_12h()

#include <notifications/awake.h>
#include <notifications/haptic.h>
#include <notifications/sound.h>

#include <gst/gst.h>

#include <glib/gi18n.h>

#include <chrono>
#include <set>
#include <string>

namespace uin = unity::indicator::notifications;

namespace unity {
namespace indicator {
namespace datetime {

/***
****
***/

class Snap::Impl
{
public:

    Impl(const std::shared_ptr<unity::indicator::notifications::Engine>& engine,
         const std::shared_ptr<const Settings>& settings):
      m_engine(engine),
      m_settings(settings)
    {
    }

    ~Impl()
    {
        for (const auto& key : m_notifications)
            m_engine->close (key);
    }

    void operator()(const Appointment& appointment,
                    const Alarm& alarm,
                    const std::string& text_1, alarm_func action_1,
                    const std::string& text_2, alarm_func action_2)
    {
        // force the system to stay awake
        auto awake = std::make_shared<uin::Awake>(m_engine->app_name());

        // create the sound...
        const auto uri = get_alarm_uri(alarm, m_settings);
        const auto volume = m_settings->alarm_volume.get();
        const bool loop = m_engine->supports_actions();
        auto sound = std::make_shared<uin::Sound>(uri, volume, loop);

        // create the haptic feedback...
        const auto haptic_mode = m_settings->alarm_haptic.get();
        std::shared_ptr<uin::Haptic> haptic;
        if (haptic_mode == "pulse")
            haptic = std::make_shared<uin::Haptic>(uin::Haptic::MODE_PULSE);

        // show a notification...
        const bool interactive = m_engine->supports_actions();
        uin::Builder b;
        b.set_body (alarm.text);
        b.set_icon_name ("alarm-clock");
        b.add_hint (uin::Builder::HINT_SNAP);
        b.add_hint (uin::Builder::HINT_AFFIRMATIVE_HINT);
        b.add_hint (uin::Builder::HINT_NONSHAPED_ICON);

        const char * timefmt;
        if (is_locale_12h()) {
            /** strftime(3) format for abbreviated weekday,
                hours, minutes in a 12h locale; e.g. Wed, 2:00 PM */
            timefmt = _("%a, %l:%M %p");
        } else {
            /** A strftime(3) format for abbreviated weekday,
                hours, minutes in a 24h locale; e.g. Wed, 14:00 */
            timefmt = _("%a, %H:%M");
        }
        const auto timestr = appointment.begin.format(timefmt);
        auto title = g_strdup_printf(_("Alarm %s"), timestr.c_str());
        b.set_title (title);
        g_free (title);
        if (alarm.duration != std::chrono::seconds::zero())
            b.set_timeout(alarm.duration);
        else {
            const auto minutes = std::chrono::minutes(m_settings->alarm_duration.get());
            b.set_timeout(std::chrono::duration_cast<std::chrono::seconds>(minutes));
        }

        if (interactive) {
            b.add_action ("action-1", text_1);
            b.add_action ("action-2", text_2);
        }

        // add 'sound', 'haptic', and 'awake' objects to the capture so
        // they stay alive until the closed callback is called; i.e.,
        // for the lifespan of the notficiation
        b.set_closed_callback([appointment, alarm, action_1, action_2, sound, awake, haptic]
                              (const std::string& action){
            if (action == "action-1")
                action_1(appointment, alarm);
            else
                action_2(appointment, alarm);
        });

        const auto key = m_engine->show(b);
        if (key)
            m_notifications.insert (key);
    }

private:

    std::string get_alarm_uri(const Alarm& alarm,
                              const std::shared_ptr<const Settings>& settings) const
    {
        const char* FALLBACK {"/usr/share/sounds/ubuntu/ringtones/Suru arpeggio.ogg"};

        const std::string candidates[] = { alarm.audio_url,
                                           settings->alarm_sound.get(),
                                           FALLBACK };

        std::string uri;

        for(const auto& candidate : candidates)
        {
            if (gst_uri_is_valid (candidate.c_str()))
            {
                uri = candidate;
                break;
            }
            else if (g_file_test(candidate.c_str(), G_FILE_TEST_EXISTS))
            {
                gchar* tmp = gst_filename_to_uri(candidate.c_str(), nullptr);
                if (tmp != nullptr)
                {
                    uri = tmp;
                    g_free (tmp);
                    break;
                }
            }
        }

        return uri;
    }

    const std::shared_ptr<unity::indicator::notifications::Engine> m_engine;
    const std::shared_ptr<const Settings> m_settings;
    std::set<int> m_notifications;
};

/***
****
***/

Snap::Snap(const std::shared_ptr<unity::indicator::notifications::Engine>& engine,
           const std::shared_ptr<const Settings>& settings):
  impl(new Impl(engine, settings))
{
}

Snap::~Snap()
{
}

void
Snap::operator()(const Appointment& appointment,
                 const Alarm& alarm,
                 const std::string& text_1, alarm_func action_1,
                 const std::string& text_2, alarm_func action_2)
{
    (*impl)(appointment,
            alarm,
            text_1, action_1,
            text_2, action_2);
}

/***
****
***/

} // namespace datetime
} // namespace indicator
} // namespace unity
