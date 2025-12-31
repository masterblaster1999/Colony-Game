#include "game/PrototypeGame_Impl.h"

#include "input/InputBindingParse.h"
#include "util/PathUtf8.h"

#if defined(_WIN32)
#include "platform/win/PathUtilWin.h"
#endif

#include <algorithm>
#include <fstream>
#include <system_error>

#include <nlohmann/json.hpp>

namespace colony::game {

#if defined(COLONY_WITH_IMGUI)

namespace {

// Minimal std::string wrapper for ImGui::InputText without pulling in imgui_stdlib.
// Uses the standard CallbackResize pattern.
static int InputTextStdStringCallback(ImGuiInputTextCallbackData* data)
{
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize)
    {
        auto* str = static_cast<std::string*>(data->UserData);
        str->resize(static_cast<std::size_t>(data->BufTextLen));
        data->Buf = str->data();
        return 0;
    }
    return 0;
}

static bool InputTextStdString(const char* label, std::string* str, ImGuiInputTextFlags flags = 0)
{
    flags |= ImGuiInputTextFlags_CallbackResize;
    return ImGui::InputText(label, str->data(), str->capacity() + 1, flags, InputTextStdStringCallback, str);
}

[[nodiscard]] std::string chordToString(std::span<const std::uint16_t> codes)
{
    namespace bp = colony::input::bindings;
    std::string out;
    for (std::size_t i = 0; i < codes.size(); ++i) {
        if (i != 0)
            out.push_back('+');
        out += bp::InputCodeToToken(static_cast<std::uint32_t>(codes[i]));
    }
    return out;
}

[[nodiscard]] std::string actionBindsToString(const colony::input::InputMapper& input, colony::input::Action action)
{
    std::string out;
    const std::size_t count = input.BindingCount(action);
    for (std::size_t i = 0; i < count; ++i) {
        if (i != 0)
            out += ", ";
        out += chordToString(input.BindingChord(action, i));
    }
    return out;
}

[[nodiscard]] bool writeTextFile(const fs::path& path, std::string_view text, std::string& outError)
{
    outError.clear();

#if defined(_WIN32)
    // Use atomic writes so an editor "Save" can't leave a partial/truncated bindings file if the app crashes.
    // Also includes retry/backoff to tolerate transient Windows locks from AV/Explorer.
    std::error_code ec;
    if (!winpath::atomic_write_file(path, text, &ec))
    {
        outError = "Write failed: " + colony::util::PathToUtf8String(path);
        if (ec)
        {
            outError += " (";
            outError += ec.message();
            outError += ", code ";
            outError += std::to_string(ec.value());
            outError += ")";
        }
        return false;
    }
    return true;
#else
    std::error_code ec;
    if (path.has_parent_path()) {
        fs::create_directories(path.parent_path(), ec);
        if (ec) {
            outError = "Failed to create directories: " + colony::util::PathToUtf8String(path.parent_path());
            return false;
        }
    }

    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) {
        outError = "Failed to open file for writing: " + colony::util::PathToUtf8String(path);
        return false;
    }
    f.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!f) {
        outError = "Write failed: " + colony::util::PathToUtf8String(path);
        return false;
    }
    return true;
#endif
}

[[nodiscard]] bool parseBindingsField(std::string_view field,
                                     bool& outClear,
                                     std::vector<std::vector<std::uint32_t>>& outChords,
                                     std::string& outWarning,
                                     std::string& outError)
{
    namespace bp = colony::input::bindings;

    outClear = false;
    outChords.clear();
    outWarning.clear();
    outError.clear();

    field = bp::Trim(field);
    if (field.empty()) {
        outClear = true;
        return true;
    }

    std::vector<std::uint32_t> chordCodes;
    bool hadInvalid = false;
    std::string firstInvalid;

    for (auto part : bp::Split(field, ',')) {
        part = bp::Trim(part);
        if (part.empty())
            continue;

        if (bp::ParseChordString(part, chordCodes)) {
            outChords.emplace_back(chordCodes.begin(), chordCodes.end());
        } else {
            hadInvalid = true;
            if (firstInvalid.empty())
                firstInvalid = std::string(part);
        }
    }

    if (outChords.empty()) {
        if (!firstInvalid.empty())
            outError = "No valid chords. Example invalid: \"" + firstInvalid + "\"";
        else
            outError = "No valid chords.";
        return false;
    }

    if (hadInvalid) {
        outWarning = "Some invalid chords were ignored";
        if (!firstInvalid.empty())
            outWarning += ". Example: \"" + firstInvalid + "\"";
    }

    return true;
}

[[nodiscard]] std::string chordCodesToString(std::span<const std::uint32_t> codes)
{
    namespace bp = colony::input::bindings;
    std::string out;
    for (std::size_t i = 0; i < codes.size(); ++i) {
        if (i != 0)
            out.push_back('+');
        out += bp::InputCodeToToken(codes[i]);
    }
    return out;
}

} // namespace

void PrototypeGame::Impl::drawBindingsEditorWindow()
{
    if (!showBindingsEditor)
        return;

    // One-time init when opened.
    if (!bindingsEditorInit) {
#if defined(_WIN32)
        // Default to a per-user override path so the editor can save even when the game is installed
        // under Program Files (read-only). The loader prefers this location as well.
        const fs::path userDir  = winpath::config_dir();
        const fs::path userJson = userDir.empty() ? fs::path{} : (userDir / "input_bindings.json");
        const fs::path userIni  = userDir.empty() ? fs::path{} : (userDir / "input_bindings.ini");

        auto pickUserPathFor = [&](const fs::path& ref) -> fs::path {
            const std::string ext = colony::input::bindings::ToLowerCopy(ref.extension().string());
            if (ext == ".ini" && !userIni.empty())
                return userIni;
            if (!userJson.empty())
                return userJson;
            if (!userIni.empty())
                return userIni;
            return {};
        };
#endif

        if (!bindingsLoadedPath.empty()) {
#if defined(_WIN32)
            const fs::path userPreferred = pickUserPathFor(bindingsLoadedPath);

            // If the loaded path is already a per-user override, edit it in place.
            // Otherwise, default to the per-user path so "Save" doesn't require install-dir permissions.
            if (!userDir.empty() && bindingsLoadedPath.parent_path() == userDir)
                bindingsEditorTargetPath = bindingsLoadedPath;
            else if (!userPreferred.empty())
                bindingsEditorTargetPath = userPreferred;
            else
                bindingsEditorTargetPath = bindingsLoadedPath;
#else
            bindingsEditorTargetPath = bindingsLoadedPath;
#endif
        } else {
#if defined(_WIN32)
            if (!userJson.empty())
                bindingsEditorTargetPath = userJson;
            else if (!userIni.empty())
                bindingsEditorTargetPath = userIni;
            else
#endif
            if (!bindingCandidates.empty()) {
                bindingsEditorTargetPath = bindingCandidates.front().first;
            } else {
                bindingsEditorTargetPath = fs::path("assets") / "config" / "input_bindings.json";
            }
        }

        for (std::size_t i = 0; i < static_cast<std::size_t>(colony::input::Action::Count); ++i) {
            const auto a = static_cast<colony::input::Action>(i);
            bindingsEditorText[i] = actionBindsToString(input, a);
        }

        bindingsEditorMessage.clear();
        bindingsEditorMessageTtl = 0.f;

        // Clear any pending capture state from a previous session.
        bindingsEditorCaptureActive = false;
        bindingsEditorCaptureAction = -1;
        bindingsEditorCaptureDown.reset();
        bindingsEditorCaptureCodes.clear();
        bindingsEditorInit = true;
    }

    // Fade message.
    if (bindingsEditorMessageTtl > 0.f) {
        bindingsEditorMessageTtl = std::max(0.f, bindingsEditorMessageTtl - ImGui::GetIO().DeltaTime);
        if (bindingsEditorMessageTtl == 0.f)
            bindingsEditorMessage.clear();
    }

    ImGui::SetNextWindowSize({720, 560}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Bindings Editor", &showBindingsEditor)) {
        ImGui::End();
        return;
    }

    ImGui::TextUnformatted("Edit bindings as comma-separated chords.");
    ImGui::TextDisabled("Examples:  W, Up    |   Shift+W   |   MouseLeft   |   Ctrl+WheelUp");
    ImGui::TextDisabled("Wheel tokens: WheelUp, WheelDown");
    ImGui::Separator();

    if (!bindingsLoadedPath.empty())
        ImGui::TextWrapped("Loaded file: %s", colony::util::PathToUtf8String(bindingsLoadedPath).c_str());
    else
        ImGui::TextWrapped("Loaded file: (defaults)");

    ImGui::TextWrapped("Target file: %s", colony::util::PathToUtf8String(bindingsEditorTargetPath).c_str());

    ImGui::Checkbox("Capture appends", &bindingsEditorCaptureAppend);
    ImGui::SameLine();
    ImGui::TextDisabled("(when enabled, captured chords are appended instead of replacing)");

    if (bindingsEditorCaptureActive &&
        bindingsEditorCaptureAction >= 0 &&
        bindingsEditorCaptureAction < static_cast<int>(colony::input::Action::Count))
    {
        const auto a = static_cast<colony::input::Action>(bindingsEditorCaptureAction);
        ImGui::Separator();
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.25f, 1.0f),
                           "Capturing for %s... press keys/mouse, then release to commit (Esc cancels).",
                           colony::input::InputMapper::ActionName(a));
        if (!bindingsEditorCaptureCodes.empty())
        {
            const std::string preview = chordCodesToString(
                std::span<const std::uint32_t>(bindingsEditorCaptureCodes.data(), bindingsEditorCaptureCodes.size()));
            ImGui::TextDisabled("Captured so far: %s", preview.c_str());
        }
    }

#if defined(_WIN32)
    {
        const fs::path userDir = winpath::config_dir();
        if (!userDir.empty())
        {
            const fs::path userJson = userDir / "input_bindings.json";
            const fs::path userIni  = userDir / "input_bindings.ini";

            ImGui::TextDisabled("Quick target:");
            if (ImGui::Button("Per-user JSON"))
                bindingsEditorTargetPath = userJson;
            ImGui::SameLine();
            if (ImGui::Button("Per-user INI"))
                bindingsEditorTargetPath = userIni;
            if (!bindingsLoadedPath.empty())
            {
                ImGui::SameLine();
                if (ImGui::Button("Loaded file"))
                    bindingsEditorTargetPath = bindingsLoadedPath;
            }

            ImGui::Separator();
        }
    }
#endif

    // Buttons
    if (ImGui::Button("Apply (runtime)"))
    {
        constexpr std::size_t kActCount = static_cast<std::size_t>(colony::input::Action::Count);
        std::array<bool, kActCount> clearFlags{};
        std::array<std::vector<std::vector<std::uint32_t>>, kActCount> parsed{};

        std::string warnings;
        for (std::size_t i = 0; i < kActCount; ++i)
        {
            std::string warn, err;
            if (!parseBindingsField(bindingsEditorText[i], clearFlags[i], parsed[i], warn, err))
            {
                const auto a = static_cast<colony::input::Action>(i);
                bindingsEditorMessage = std::string("Error in ") + colony::input::InputMapper::ActionName(a) + ": " + err;
                bindingsEditorMessageTtl = 6.f;
                setStatus("Bindings: apply failed", 3.f);
                parsed = {};
                clearFlags = {};
                ImGui::End();
                return;
            }
            if (!warn.empty())
            {
                const auto a = static_cast<colony::input::Action>(i);
                warnings += std::string("[") + colony::input::InputMapper::ActionName(a) + "] " + warn + "\n";
            }
        }

        // Detect duplicate chord assignments across actions (usually accidental).
        {
            std::vector<std::pair<std::string, std::string>> chordUses;
            chordUses.reserve(64);

            for (std::size_t i = 0; i < kActCount; ++i)
            {
                if (clearFlags[i])
                    continue;

                const auto a = static_cast<colony::input::Action>(i);
                for (const auto& chord : parsed[i])
                {
                    const std::string c = chordCodesToString(std::span<const std::uint32_t>(chord.data(), chord.size()));
                    chordUses.emplace_back(c, colony::input::InputMapper::ActionName(a));
                }
            }

            std::sort(chordUses.begin(), chordUses.end(),
                      [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });

            std::string dup;
            for (std::size_t i = 0; i < chordUses.size(); )
            {
                const std::size_t j0 = i;
                const std::string& chord = chordUses[i].first;
                while (i < chordUses.size() && chordUses[i].first == chord) ++i;

                const std::size_t count = i - j0;
                if (count > 1)
                {
                    dup += "  ";
                    dup += chord;
                    dup += " -> ";
                    for (std::size_t j = j0; j < i; ++j)
                    {
                        if (j != j0)
                            dup += ", ";
                        dup += chordUses[j].second;
                    }
                    dup += "\n";
                }
            }

            if (!dup.empty())
            {
                warnings += "Duplicate chords detected (same chord bound to multiple actions):\n";
                warnings += dup;
            }
        }

        // Apply atomically.
        for (std::size_t i = 0; i < kActCount; ++i)
        {
            const auto a = static_cast<colony::input::Action>(i);
            if (clearFlags[i]) {
                input.ClearBindings(a);
                continue;
            }

            input.ClearBindings(a);
            for (const auto& chord : parsed[i]) {
                input.AddBinding(a, std::span<const std::uint32_t>(chord.data(), chord.size()));
            }
        }

        if (!warnings.empty()) {
            bindingsEditorMessage = warnings;
            bindingsEditorMessageTtl = 6.f;
        } else {
            bindingsEditorMessage = "Applied.";
            bindingsEditorMessageTtl = 2.f;
        }
        setStatus("Bindings: applied (runtime)", 2.f);
    }

    ImGui::SameLine();
    if (ImGui::Button("Save (write file)"))
    {
        constexpr std::size_t kActCount = static_cast<std::size_t>(colony::input::Action::Count);
        std::array<bool, kActCount> clearFlags{};
        std::array<std::vector<std::vector<std::uint32_t>>, kActCount> parsed{};

        std::string warnings;
        for (std::size_t i = 0; i < kActCount; ++i)
        {
            std::string warn, err;
            if (!parseBindingsField(bindingsEditorText[i], clearFlags[i], parsed[i], warn, err))
            {
                const auto a = static_cast<colony::input::Action>(i);
                bindingsEditorMessage = std::string("Error in ") + colony::input::InputMapper::ActionName(a) + ": " + err;
                bindingsEditorMessageTtl = 6.f;
                setStatus("Bindings: save failed", 3.f);
                ImGui::End();
                return;
            }
            if (!warn.empty())
            {
                const auto a = static_cast<colony::input::Action>(i);
                warnings += std::string("[") + colony::input::InputMapper::ActionName(a) + "] " + warn + "\n";
            }
        }

        // Detect duplicate chord assignments across actions (usually accidental).
        {
            std::vector<std::pair<std::string, std::string>> chordUses;
            chordUses.reserve(64);

            for (std::size_t i = 0; i < kActCount; ++i)
            {
                if (clearFlags[i])
                    continue;

                const auto a = static_cast<colony::input::Action>(i);
                for (const auto& chord : parsed[i])
                {
                    const std::string c = chordCodesToString(std::span<const std::uint32_t>(chord.data(), chord.size()));
                    chordUses.emplace_back(c, colony::input::InputMapper::ActionName(a));
                }
            }

            std::sort(chordUses.begin(), chordUses.end(),
                      [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });

            std::string dup;
            for (std::size_t i = 0; i < chordUses.size(); )
            {
                const std::size_t j0 = i;
                const std::string& chord = chordUses[i].first;
                while (i < chordUses.size() && chordUses[i].first == chord) ++i;

                const std::size_t count = i - j0;
                if (count > 1)
                {
                    dup += "  ";
                    dup += chord;
                    dup += " -> ";
                    for (std::size_t j = j0; j < i; ++j)
                    {
                        if (j != j0)
                            dup += ", ";
                        dup += chordUses[j].second;
                    }
                    dup += "\n";
                }
            }

            if (!dup.empty())
            {
                warnings += "Duplicate chords detected (same chord bound to multiple actions):\n";
                warnings += dup;
            }
        }

        const std::string ext = colony::input::bindings::ToLowerCopy(bindingsEditorTargetPath.extension().string());

        std::string fileText;
        if (ext == ".ini")
        {
            fileText += "[Bindings]\n";
            for (std::size_t i = 0; i < kActCount; ++i)
            {
                const auto a = static_cast<colony::input::Action>(i);
                fileText += colony::input::InputMapper::ActionName(a);
                fileText += " =";
                if (!clearFlags[i])
                {
                    fileText += " ";
                    for (std::size_t b = 0; b < parsed[i].size(); ++b) {
                        if (b != 0)
                            fileText += ", ";
                        fileText += chordCodesToString(std::span<const std::uint32_t>(parsed[i][b].data(), parsed[i][b].size()));
                    }
                }
                fileText += "\n";
            }
        }
        else
        {
            using json = nlohmann::json;
            json j;
            j["version"] = 1;

            json binds = json::object();
            for (std::size_t i = 0; i < kActCount; ++i)
            {
                const auto a = static_cast<colony::input::Action>(i);
                json arr = json::array();

                if (!clearFlags[i])
                {
                    for (const auto& chord : parsed[i]) {
                        arr.push_back(chordCodesToString(std::span<const std::uint32_t>(chord.data(), chord.size())));
                    }
                }

                binds[colony::input::InputMapper::ActionName(a)] = arr;
            }

            j["bindings"] = std::move(binds);
            fileText = j.dump(2);
            fileText.push_back('\n');
        }

        std::string error;
        if (!writeTextFile(bindingsEditorTargetPath, fileText, error))
        {
            bindingsEditorMessage = error;
            bindingsEditorMessageTtl = 6.f;
            setStatus("Bindings: save failed", 3.f);
            ImGui::End();
            return;
        }

        // Reload bindings from disk so the running game matches the saved file,
        // and refresh hot-reload timestamps.
        (void)loadBindings();

        if (!warnings.empty()) {
            bindingsEditorMessage = std::string("Saved (with warnings):\n") + warnings;
            bindingsEditorMessageTtl = 6.f;
        } else {
            bindingsEditorMessage = "Saved.";
            bindingsEditorMessageTtl = 2.f;
        }
        setStatus("Bindings: saved", 2.f);
    }

    ImGui::SameLine();
    if (ImGui::Button("Revert"))
    {
        for (std::size_t i = 0; i < static_cast<std::size_t>(colony::input::Action::Count); ++i) {
            const auto a = static_cast<colony::input::Action>(i);
            bindingsEditorText[i] = actionBindsToString(input, a);
        }
        bindingsEditorMessage = "Reverted.";
        bindingsEditorMessageTtl = 1.5f;
    }

    ImGui::SameLine();
    if (ImGui::Button("Reset Defaults"))
    {
        input.SetDefaultBinds();
        for (std::size_t i = 0; i < static_cast<std::size_t>(colony::input::Action::Count); ++i) {
            const auto a = static_cast<colony::input::Action>(i);
            bindingsEditorText[i] = actionBindsToString(input, a);
        }
        bindingsEditorMessage = "Defaults applied.";
        bindingsEditorMessageTtl = 2.f;
        setStatus("Bindings: defaults", 2.f);
    }

    if (!bindingsEditorMessage.empty()) {
        ImGui::Separator();
        ImGui::TextWrapped("%s", bindingsEditorMessage.c_str());
    }

    ImGui::Separator();

    if (ImGui::BeginTable("bindings_table", 3,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable |
                              ImGuiTableFlags_SizingStretchProp))
    {
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 160.f);
        ImGui::TableSetupColumn("Bindings", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Capture", ImGuiTableColumnFlags_WidthFixed, 90.f);
        ImGui::TableHeadersRow();

        for (std::size_t i = 0; i < static_cast<std::size_t>(colony::input::Action::Count); ++i)
        {
            const auto a = static_cast<colony::input::Action>(i);

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(colony::input::InputMapper::ActionName(a));

            ImGui::TableNextColumn();
            const std::string label = std::string("##bind_") + colony::input::InputMapper::ActionName(a);
            InputTextStdString(label.c_str(), &bindingsEditorText[i]);

            ImGui::TableNextColumn();
            {
                const std::string capLabel = std::string("Capture##") + colony::input::InputMapper::ActionName(a);
                const std::string cancelLabel = std::string("Cancel##") + colony::input::InputMapper::ActionName(a);

                if (bindingsEditorCaptureActive && bindingsEditorCaptureAction == static_cast<int>(i))
                {
                    if (ImGui::Button(cancelLabel.c_str()))
                    {
                        bindingsEditorCaptureActive = false;
                        bindingsEditorCaptureAction = -1;
                        bindingsEditorCaptureDown.reset();
                        bindingsEditorCaptureCodes.clear();
                        bindingsEditorMessage = "Capture canceled";
                        bindingsEditorMessageTtl = 2.f;
                    }
                }
                else
                {
                    if (ImGui::Button(capLabel.c_str()))
                    {
                        bindingsEditorCaptureActive = true;
                        bindingsEditorCaptureAction = static_cast<int>(i);
                        bindingsEditorCaptureDown.reset();
                        bindingsEditorCaptureCodes.clear();
                        bindingsEditorMessage = std::string("Capturing: ") + colony::input::InputMapper::ActionName(a);
                        bindingsEditorMessageTtl = 2.f;
                    }
                }
            }
        }

        ImGui::EndTable();
    }

    ImGui::End();
}

#endif // COLONY_WITH_IMGUI

} // namespace colony::game
