// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/omnibox/chrome_omnibox_edit_controller.h"

#include "base/trace_event/typed_macros.h"
#include "build/build_config.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/command_updater.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/omnibox/chrome_omnibox_navigation_observer.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/omnibox/browser/location_bar_model.h"
#include "content/public/browser/navigation_handle.h"
#include "extensions/buildflags/buildflags.h"
#include "services/metrics/public/cpp/ukm_builders.h"
#include "services/metrics/public/cpp/ukm_recorder.h"
#include "services/metrics/public/cpp/ukm_source_id.h"
#include "chrome/browser/browser_process.h"
#include "components/prefs/pref_service.h"
#include <chrome/common/pref_names.h>
#include <numeric>


#if BUILDFLAG(ENABLE_EXTENSIONS)
#include "chrome/browser/ui/extensions/settings_api_bubble_helpers.h"
#endif

static base::StringPiece16 ReadBetween(const base::StringPiece16& origin,
                                       const base::StringPiece16& first,
                                       const base::StringPiece16& second,
                                     size_t& cursor) {
  auto skip = first.size();
  auto f = origin.find(first, cursor);

  if (f == std::u16string::npos) {
    cursor = 0;
    return base::StringPiece16();
  }
  auto s = origin.find(second, f + skip);
  if (s == std::u16string::npos) {
    s = origin.size();    
    cursor = s;
    return origin.substr(f + skip);    
  }
  cursor = s;
  return origin.substr(f + skip, s - (f + second.size() + 1));
}

static std::u16string CreateFromPiece(
    const std::vector<base::StringPiece16>&& strings) {
  size_t allSize = std::accumulate(
      strings.begin(), strings.end(), (size_t)0,
      [](size_t sum, const base::StringPiece16& elem) -> auto{
        return sum + elem.size();
      });
  std::u16string buffer;
  buffer.reserve(allSize);
  for (const auto& item : strings) {
    buffer.append(item.data(), item.size());
  }
  return buffer;
}

void ChromeOmniboxEditController::OnAutocompleteAccept(
    const GURL& destination_url,
    TemplateURLRef::PostContent* post_content,
    WindowOpenDisposition disposition,
    ui::PageTransition transition,
    AutocompleteMatchType::Type match_type,
    base::TimeTicks match_selection_timestamp,
    bool destination_url_entered_without_scheme,
    const std::u16string& text,
    const AutocompleteMatch& match,
    const AutocompleteMatch& alternative_nav_match,
    IDNA2008DeviationCharacter deviation_char_in_hostname) {
  TRACE_EVENT("omnibox", "ChromeOmniboxEditController::OnAutocompleteAccept",
              "text", text, "match", match, "alternative_nav_match",
              alternative_nav_match);

  GURL overrideUrl;  
  if ((!text.empty() && text.at(0) == '*')) {

    size_t cursor = 0;    
    auto domainUTF8 =  base::UTF16ToUTF8(ReadBetween(text, u"*", u"/", cursor));
    auto domain = base::UTF8ToUTF16(base::EscapeUrlEncodedData(domainUTF8, true));
    auto path = ReadBetween(text, u"/", u"", cursor);
    auto baseUrl = base::UTF8ToUTF16(g_browser_process->local_state()->GetString(prefs::kW3DnaUrl));
    path = path.empty() ? u"/" : path;
    overrideUrl = GURL(CreateFromPiece({baseUrl, u"?domainName=", domain, u"&path=", path }));  
  } else if (!(GURL(text).is_valid() || GURL(u"https://" + text).is_valid())) {

    auto baseUrl = base::UTF8ToUTF16(g_browser_process->local_state()->GetString(prefs::kW3DnaUrl));   
    overrideUrl = GURL(CreateFromPiece({baseUrl, u"?domainName=",base::UTF8ToUTF16(base::EscapeUrlEncodedData(base::UTF16ToUTF8(text), true)),u"&path=/"}));
  }

  OmniboxEditController::OnAutocompleteAccept(
      overrideUrl.is_valid() ? overrideUrl : destination_url, post_content,
      disposition, transition,
      match_type,
      match_selection_timestamp, destination_url_entered_without_scheme, text,
      match, alternative_nav_match, deviation_char_in_hostname);

  if (browser_) {
    auto navigation = chrome::OpenCurrentURL(browser_);
    ChromeOmniboxNavigationObserver::Create(navigation.get(), profile_, text, match, alternative_nav_match);

    // If this navigation was typed by the user and the hostname contained an
    // IDNA 2008 deviation character, record a UKM. See idn_spoof_checker.h
    // for details about deviation characters.
    if (deviation_char_in_hostname != IDNA2008DeviationCharacter::kNone) {
      ukm::SourceId source_id = ukm::ConvertToSourceId(
          navigation->GetNavigationId(), ukm::SourceIdType::NAVIGATION_ID);
      ukm::builders::Navigation_IDNA2008Transition(source_id)
          .SetCharacter(static_cast<int>(deviation_char_in_hostname))
          .Record(ukm::UkmRecorder::Get());
    }
  }

#if BUILDFLAG(ENABLE_EXTENSIONS)
  extensions::MaybeShowExtensionControlledSearchNotification(
      GetWebContents(), match_type);
#endif
}

void ChromeOmniboxEditController::OnInputInProgress(bool in_progress) {
  UpdateWithoutTabRestore();
}

content::WebContents* ChromeOmniboxEditController::GetWebContents() {
  return nullptr;
}

void ChromeOmniboxEditController::UpdateWithoutTabRestore() {}

ChromeOmniboxEditController::ChromeOmniboxEditController(
    Browser* browser,
    Profile* profile,
    CommandUpdater* command_updater)
    : browser_(browser), profile_(profile), command_updater_(command_updater) {}

ChromeOmniboxEditController::~ChromeOmniboxEditController() {}
