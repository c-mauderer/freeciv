/***********************************************************************
 Freeciv - Copyright (C) 1996 - A Kjeldberg, L Gregersen, P Unold
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
***********************************************************************/

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

/* ANSI */
#ifdef HAVE_STRING_H
#include <string.h>
#endif

/* utility */
#include "capability.h"
#include "log.h"
#include "registry.h"

/* common */
#include "actions.h"
#include "effects.h"
#include "fc_types.h"
#include "game.h"
#include "movement.h"
#include "requirements.h"
#include "unittype.h"

/* server */
#include "rssanity.h"
#include "ruleset.h"

#include "rscompat.h"

#define enough_new_user_flags(_new_flags_, _name_,                        \
                              _LAST_USER_FLAG_, _LAST_USER_FLAG_PREV_)    \
FC_STATIC_ASSERT((ARRAY_SIZE(_new_flags_)                                 \
                  <= _LAST_USER_FLAG_ - _LAST_USER_FLAG_PREV_),           \
                 not_enough_new_##_name_##_user_flags)

#define UTYF_LAST_USER_FLAG_3_0 UTYF_USER_FLAG_40
#define UCF_LAST_USER_FLAG_3_0 UCF_USER_FLAG_8
#define TER_LAST_USER_FLAG_3_0 TER_USER_8

/**********************************************************************//**
  Initialize rscompat information structure
**************************************************************************/
void rscompat_init_info(struct rscompat_info *info)
{
  memset(info, 0, sizeof(*info));
}

/**********************************************************************//**
  Ruleset files should have a capabilities string datafile.options
  This checks the string and that the required capabilities are satisfied.
**************************************************************************/
int rscompat_check_capabilities(struct section_file *file,
                                const char *filename,
                                const struct rscompat_info *info)
{
  const char *datafile_options;
  bool ok = FALSE;
  int format;

  if (!(datafile_options = secfile_lookup_str(file, "datafile.options"))) {
    log_fatal("\"%s\": ruleset capability problem:", filename);
    ruleset_error(LOG_ERROR, "%s", secfile_error());

    return 0;
  }

  if (info->compat_mode) {
    /* Check alternative capstr first, so that when we do the main capstr check,
     * we already know that failures there are fatal (error message correct, can return
     * immediately) */

    if (has_capabilities(RULESET_COMPAT_CAP, datafile_options)
        && has_capabilities(datafile_options, RULESET_COMPAT_CAP)) {
      ok = TRUE;
    }
  }

  if (!ok) {
    if (!has_capabilities(RULESET_CAPABILITIES, datafile_options)) {
      log_fatal("\"%s\": ruleset datafile appears incompatible:", filename);
      log_fatal("  datafile options: %s", datafile_options);
      log_fatal("  supported options: %s", RULESET_CAPABILITIES);
      ruleset_error(LOG_ERROR, "Capability problem");

      return 0;
    }
    if (!has_capabilities(datafile_options, RULESET_CAPABILITIES)) {
      log_fatal("\"%s\": ruleset datafile claims required option(s)"
                " that we don't support:", filename);
      log_fatal("  datafile options: %s", datafile_options);
      log_fatal("  supported options: %s", RULESET_CAPABILITIES);
      ruleset_error(LOG_ERROR, "Capability problem");

      return 0;
    }
  }

  if (!secfile_lookup_int(file, &format, "datafile.format_version")) {
    log_error("\"%s\": lacking legal format_version field", filename);
    ruleset_error(LOG_ERROR, "%s", secfile_error());

    return 0;
  } else if (format == 0) {
    log_error("\"%s\": Illegal format_version value", filename);
    ruleset_error(LOG_ERROR, "Format version error");
  }

  return format;
}
/**********************************************************************//**
  Different ruleset files within a ruleset directory should all have
  identical datafile.format_version
  This checks the file version against the expected version.

  See also rscompat_check_capabilities
**************************************************************************/
bool rscompat_check_cap_and_version(struct section_file *file,
                                    const char *filename,
                                    const struct rscompat_info *info)
{
  int format_version;

  fc_assert_ret_val(info->version > 0, FALSE);

  format_version = rscompat_check_capabilities(file, filename, info);
  if (format_version <= 0) {
    /* Already logged in rscompat_check_capabilities */
    return FALSE;
  }

  if (format_version != info->version) {
    log_fatal("\"%s\": ruleset datafile format version differs from"
              " other ruleset datafile(s):", filename);
    log_fatal("  datafile format version: %d", format_version);
    log_fatal("  expected format version: %d", info->version);
    ruleset_error(LOG_ERROR, "Inconsistent format versions");

    return FALSE;
  }

  return TRUE;
}

/**********************************************************************//**
  Add all hard obligatory requirements to an action enabler or disable it.
  @param ae the action enabler to add requirements to.
  @return TRUE iff adding obligatory hard reqs for the enabler's action
               needs to restart - say if an enabler was added or removed.
**************************************************************************/
static bool
rscompat_enabler_add_obligatory_hard_reqs(struct action_enabler *ae)
{
  struct req_vec_problem *problem;

  struct action *paction = action_by_number(ae->action);
  /* Some changes requires starting to process an action's enablers from
   * the beginning. */
  bool needs_restart = FALSE;

  while ((problem = action_enabler_suggest_repair(ae)) != NULL) {
    /* A hard obligatory requirement is missing. */

    int i;

    if (problem->num_suggested_solutions == 0) {
      /* Didn't get any suggestions about how to solve this. */

      log_error("While adding hard obligatory reqs to action enabler"
                 " for %s: %s"
                 " Don't know how to fix it."
                 " Dropping it.",
                action_rule_name(paction), problem->description);
      ae->ruledit_disabled = TRUE;

      req_vec_problem_free(problem);
      return TRUE;
    }

    /* Sanity check. */
    fc_assert_ret_val(problem->num_suggested_solutions > 0,
                      needs_restart);

    /* Only append is supported for upgrade */
    for (i = 0; i < problem->num_suggested_solutions; i++) {
      if (problem->suggested_solutions[i].operation != RVCO_APPEND) {
        /* A problem that isn't caused by missing obligatory hard
         * requirements has been detected.
         *
         * Probably an old requirement that contradicted a hard requirement
         * that wasn't documented by making it obligatory. In that case all
         * suggested solutions has been applied to the enabler creating a
         * new copy for each possible fulfillment of the new obligatory hard
         * requirement.
         *
         * If another copy of the original enabler has survived this isn't
         * an error. It probably isn't event an indication of a potential
         * problem.
         *
         * If no possible solution survives the enabler was never in use
         * because the action it self would have blocked it. In that case
         * this is an error. */

        log_warn("While adding hard obligatory reqs to action enabler"
                 " for %s: %s"
                 " Dropping it.",
                 action_rule_name(paction), problem->description);
        ae->ruledit_disabled = TRUE;
        req_vec_problem_free(problem);
        return TRUE;
      }
    }

    for (i = 0; i < problem->num_suggested_solutions; i++) {
      struct action_enabler *new_enabler;

      /* There can be more than one suggestion to apply. In that case both
       * are applied to their own copy. The original should therefore be
       * kept for now. */
      new_enabler = action_enabler_copy(ae);

      /* Apply the solution. */
      if (!req_vec_change_apply(&problem->suggested_solutions[i],
                                action_enabler_vector_by_number,
                                new_enabler)) {
        log_error("While adding hard obligatory reqs to action enabler"
                  " for %s: %s"
                  "Failed to apply solution %s."
                  " Dropping it.",
                  action_rule_name(paction), problem->description,
                  req_vec_change_translation(
                    &problem->suggested_solutions[i],
                    action_enabler_vector_by_number_name));
        new_enabler->ruledit_disabled = TRUE;
        req_vec_problem_free(problem);
        return TRUE;
      }

      if (problem->num_suggested_solutions - 1 == i) {
        /* The last modification is to the original enabler. */
        ae->action = new_enabler->action;
        ae->ruledit_disabled = new_enabler->ruledit_disabled;
        requirement_vector_copy(&ae->actor_reqs,
                                &new_enabler->actor_reqs);
        requirement_vector_copy(&ae->target_reqs,
                                &new_enabler->target_reqs);
        FC_FREE(new_enabler);
      } else {
        /* Register the new enabler */
        action_enabler_add(new_enabler);

        /* This changes the number of action enablers. */
        needs_restart = TRUE;
      }
    }

    req_vec_problem_free(problem);

    if (needs_restart) {
      /* May need to apply future upgrades to the copies too. */
      return TRUE;
    }
  }

  return needs_restart;
}

/**********************************************************************//**
  Update existing action enablers for new hard obligatory requirements.
  Disable those that can't be upgraded.
**************************************************************************/
void rscompat_enablers_add_obligatory_hard_reqs(void)
{
  log_normal("action enablers: adding obligatory hard requirements.");
  log_warn("More than one way to fulfill a new obligatory hard requirement"
           " may exist."
           " In that case the enabler is copied so each alternative"
           " solution is applied to a copy of the enabler."
           " If an action enabler becomes self contradicting after applying"
           " a solution it is dropped."
           " Note that other copies of the original enabler may have"
           " survived even if one copy is dropped.");

  action_iterate(act_id) {
    bool restart_enablers_for_action;

    do {
      restart_enablers_for_action = FALSE;
      action_enabler_list_iterate(action_enablers_for_action(act_id), ae) {
        if (ae->ruledit_disabled) {
          /* Ignore disabled enablers */
          continue;
        }
        if (rscompat_enabler_add_obligatory_hard_reqs(ae)) {
          /* Something important, probably the number of action enablers
           * for this action, changed. Start over again on this action's
           * enablers. */
          restart_enablers_for_action = TRUE;
          break;
        }
      } action_enabler_list_iterate_end;
    } while (restart_enablers_for_action);
  } action_iterate_end;
}

/**********************************************************************//**
  Do compatibility things with names before they are referred to. Runs
  after names are loaded from the ruleset but before the ruleset objects
  that may refer to them are loaded.

  This is needed when previously hard coded items that are referred to in
  the ruleset them self becomes ruleset defined.

  Returns FALSE if an error occurs.
**************************************************************************/
bool rscompat_names(struct rscompat_info *info)
{
  /* No errors encountered. */
  return TRUE;
}

/**********************************************************************//**
  Adjust effects
**************************************************************************/
static bool effect_list_compat_cb(struct effect *peffect, void *data)
{
  /* Go to the next effect. */
  return TRUE;
}

/**********************************************************************//**
  Do compatibility things after regular ruleset loading.
**************************************************************************/
void rscompat_postprocess(struct rscompat_info *info)
{
  if (!info->compat_mode) {
    /* There isn't anything here that should be done outside of compat
     * mode. */
    return;
  }

  /* Upgrade existing effects. Done before new effects are added to prevent
   * the new effects from being upgraded by accident. */
  iterate_effect_cache(effect_list_compat_cb, info);

  if (info->version < RSFORMAT_3_2) {
    struct effect *peffect;

    /* Nuke blast radius has moved to the ruleset. */
    action_iterate(act_id) {
      const struct action *paction = action_by_number(act_id);

      if (!(action_has_result(paction, ACTRES_NUKE)
            || action_has_result(paction, ACTRES_NUKE_UNITS)
            || action_has_result(paction, ACTRES_SPY_NUKE))) {
        /* Not relevant. */
        continue;
      }

      peffect = effect_new(EFT_NUKE_BLAST_RADIUS_1_SQ, 2, NULL);
      effect_req_append(peffect, req_from_str("Action", "Local",
                                              FALSE, TRUE, FALSE,
                                              action_rule_name(paction)));
    } action_iterate_end;

    /* That Attack and Bombard can't destroy a city
     * has moved to the ruleset. */
    peffect = effect_new(EFT_UNIT_NO_LOSE_POP,
                         effect_value_will_make_positive(
                             EFT_UNIT_NO_LOSE_POP),
                         NULL);
    effect_req_append(peffect, req_from_str("MinSize", "City", FALSE, FALSE,
                                            FALSE, "2"));
  }

  /* Make sure that all action enablers added or modified by the
   * compatibility post processing fulfills all hard action requirements. */
  rscompat_enablers_add_obligatory_hard_reqs();

  /* The ruleset may need adjustments it didn't need before compatibility
   * post processing.
   *
   * If this isn't done a user of ruleset compatibility that ends up using
   * the rules risks bad rules. A user that saves the ruleset rather than
   * using it risks an unexpected change on the next load and save. */
  autoadjust_ruleset_data();
}

/**********************************************************************//**
  Update improvement genus for coinage improvements.
**************************************************************************/
enum impr_genus_id rscompat_genus_3_2(struct rscompat_info *compat,
                                      const bv_impr_flags flags,
                                      enum impr_genus_id old_genus)
{
  if (compat->compat_mode && compat->version < RSFORMAT_3_2) {
    if (BV_ISSET(flags, IF_GOLD) && IG_SPECIAL == old_genus) {
      return IG_CONVERT;
    }
  }

  return old_genus;
}

/**********************************************************************//**
  Update requirement range for certain requirement types.
**************************************************************************/
const char *rscompat_req_range_3_2(struct rscompat_info *compat,
                                   const char *type,
                                   const char *old_range)
{
  if (compat->compat_mode && compat->version < RSFORMAT_3_2) {
    /* Requirement types that refer to the target tile and now use the
     * "Tile" range instead of the "Local" range */
    if (!fc_strcasecmp(req_range_name(REQ_RANGE_LOCAL), old_range)
        && (!fc_strcasecmp(universals_n_name(VUT_TERRAIN), type)
            || !fc_strcasecmp(universals_n_name(VUT_TERRAINCLASS), type)
            || !fc_strcasecmp(universals_n_name(VUT_TERRAINALTER), type)
            || !fc_strcasecmp(universals_n_name(VUT_CITYTILE), type)
            || !fc_strcasecmp(universals_n_name(VUT_TERRFLAG), type)
            || !fc_strcasecmp(universals_n_name(VUT_ROADFLAG), type)
            || !fc_strcasecmp(universals_n_name(VUT_EXTRA), type)
            || !fc_strcasecmp(universals_n_name(VUT_MAXTILEUNITS), type)
            || !fc_strcasecmp(universals_n_name(VUT_EXTRAFLAG), type))) {
      return req_range_name(REQ_RANGE_TILE);
    }
  }

  return old_range;
}

/**********************************************************************//**
  Update individual requirements.
**************************************************************************/
void rscompat_req_adjust_3_2(const struct rscompat_info *compat,
                             const char **ptype, const char **pname,
                             bool *ppresent, const char *sec_name)
{
  char buf[1024];

  if (compat->compat_mode && compat->version < RSFORMAT_3_2) {
    /* Recreate old "alltemperate" and "singlepole" ServerSetting
     * requirements with MinLatitude and MaxLatitude. */
    if (!fc_strcasecmp(universals_n_name(VUT_SERVERSETTING), *ptype)) {
      if (!fc_strcasecmp("alltemperate", *pname)) {
        /* alltemperate implies no latitudes != 500
         * !alltemperate implies latitudes 0 to 1000
         * ~> alltemperate enabled iff no latitude >= 750
         * (other numbers in [501,1000] would work as well)
         * (no latitude <= some number in [0, 499] would work as well) */
        *ptype = universals_n_name(VUT_MINLATITUDE);
        *pname = "750";
        *ppresent = !(*ppresent);

        if (compat->log_cb != NULL) {
          /* Inform the user that there are different solutions */
          fc_snprintf(buf, sizeof(buf),
                      "Replaced 'alltemperate' server setting requirement "
                      "in %s with a MinLatitude requirement. Other "
                      "equivalent requirements are possible; make sure it "
                      "makes sense.", sec_name);
          compat->log_cb(buf);
        }
      } else if (!fc_strcasecmp("singlepole", *pname)) {
        /* Assume we're updating a sane ruleset, i.e. singlepole reqs only
         * possible/relevant when alltemperate is already disabled.
         * singlepole implies no latitudes < 0
         * !singlepole implies latitudes -1000 to -1 (given !alltemperate)
         * ~> singlepole enabled iff no latitude <= -500
         * (other numbers in [-1000,-1] would work as well) */
        *ptype = universals_n_name(VUT_MAXLATITUDE);
        *pname = "-500";
        *ppresent = !(*ppresent);

        if (compat->log_cb != NULL) {
          /* Inform the user that there are different solutions */
          fc_snprintf(buf, sizeof(buf),
                      "Replaced 'singlepole' server setting requirement "
                      "in %s with a MaxLatitude requirement. Other "
                      "equivalent requirements are possible; make sure it "
                      "makes sense.", sec_name);
          compat->log_cb(buf);
        }
      }
    }
  }
}

/**********************************************************************//**
  Adjust values of an extra loaded from a 3.1 ruleset.
**************************************************************************/
void rscompat_extra_adjust_3_2(struct extra_type *pextra)
{
  /* Huts were not allowed on polar regions
   * (defined as "Frozen" - but that was just workaround we don't want to reproduce) */
  if (is_extra_caused_by(pextra, EC_HUT)) {
    requirement_vector_append(&pextra->reqs,
                              req_from_str("MaxLatitude", "Tile",
                                           FALSE, TRUE, FALSE,
                                           "980"));
    requirement_vector_append(&pextra->reqs,
                              req_from_str("MinLatitude", "Tile",
                                           FALSE, TRUE, FALSE,
                                           "-980"));
  }
}
