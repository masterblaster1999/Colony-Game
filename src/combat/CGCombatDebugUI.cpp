#include "CGCombatDebugUI.hpp"

#if __has_include(<imgui.h>)
  #include <imgui.h>
  #define CG_COMBAT_HAS_IMGUI 1
#else
  #define CG_COMBAT_HAS_IMGUI 0
#endif

#include <algorithm>
#include <cstddef>

namespace colony::combat {

void draw_combat_debug_ui(CombatSimulation& sim) {
#if CG_COMBAT_HAS_IMGUI
  if (!ImGui::Begin("Combat Debug")) {
    ImGui::End();
    return;
  }

  CombatWorld& w = sim.world();
  const auto cfg = sim.config();

  ImGui::Text("Entities: %d", static_cast<int>(w.size()));
  ImGui::Text("Fixed dt: %.4f  Max substeps: %u", cfg.fixed_dt_sec, cfg.max_substeps);
  ImGui::Separator();

  if (ImGui::CollapsingHeader("Combatants", ImGuiTreeNodeFlags_DefaultOpen)) {
    if (ImGui::BeginTable("CombatantsTable", 6,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                          ImVec2(0, 250))) {
      ImGui::TableSetupColumn("Id");
      ImGui::TableSetupColumn("Faction");
      ImGui::TableSetupColumn("Alive");
      ImGui::TableSetupColumn("HP");
      ImGui::TableSetupColumn("Weapon");
      ImGui::TableSetupColumn("Status");
      ImGui::TableHeadersRow();

      for (const Combatant& c : w.combatants()) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0); ImGui::Text("%u", c.id);
        ImGui::TableSetColumnIndex(1); ImGui::Text("%u", c.faction);
        ImGui::TableSetColumnIndex(2); ImGui::TextUnformatted(c.alive ? "yes" : "no");
        ImGui::TableSetColumnIndex(3); ImGui::Text("%.1f / %.1f", c.health, c.max_health);
        ImGui::TableSetColumnIndex(4); ImGui::TextUnformatted(c.weapon.name.c_str());

        ImGui::TableSetColumnIndex(5);
        if (c.statuses.empty()) {
          ImGui::TextUnformatted("-");
        } else {
          // Print first few effects
          std::size_t shown = 0;
          for (const StatusInstance& st : c.statuses) {
            const std::string_view n = to_string(st.type);
            ImGui::Text("%.*s (%.1fs x%u)", static_cast<int>(n.size()), n.data(),
                        st.remaining_sec, static_cast<unsigned>(st.stacks));
            if (++shown >= 3) break;
          }
          if (c.statuses.size() > shown) ImGui::Text("... (%d more)", static_cast<int>(c.statuses.size() - shown));
        }
      }

      ImGui::EndTable();
    }
  }

  if (ImGui::CollapsingHeader("Events", ImGuiTreeNodeFlags_DefaultOpen)) {
    if (ImGui::BeginChild("CombatEventsChild", ImVec2(0, 200), true)) {
      for (const CombatEvent& e : w.events()) {
        ImGui::TextUnformatted(describe_event(e).c_str());
      }
    }
    ImGui::EndChild();
  }

  ImGui::End();
#else
  (void)sim;
#endif
}

} // namespace colony::combat
