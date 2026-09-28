# Issue tracker: GitHub

Issues and specs for this repo live as GitHub issues. Use the gh CLI for all operations.

## Conventions

- Create an issue: gh issue create --title "..." --body "..."
- Read an issue: gh issue view <number> --comments, and fetch labels as needed.
- List issues: gh issue list with state, label, and JSON filters as needed.
- Comment on an issue: gh issue comment <number> --body "..."
- Apply or remove labels: gh issue edit <number> --add-label "..." or --remove-label "..."
- Close: gh issue close <number> --comment "..."

Infer the repo from git remote -v; gh does this automatically when run inside a clone.

## Pull requests as a triage surface

PRs as a request surface: no.

## When a skill says "publish to the issue tracker"

Create a GitHub issue.

## When a skill says "fetch the relevant ticket"

Run gh issue view <number> --comments.

## Wayfinding operations

Used by /wayfinder. The map is a single issue labelled wayfinder:map, holding the Notes, Decisions-so-far, and Fog body.

- Map: a single issue labelled wayfinder:map.
- Child ticket: an issue linked to the map as a GitHub sub-issue. Where sub-issues are not enabled, add the child to a task list in the map body and put Part of #<map> at the top of the child body.
- Blocking: GitHub native issue dependencies are the canonical UI-visible representation. Where dependencies are unavailable, add a Blocked by: #<n>, #<n> line near the top of the child body.
- Frontier query: list the map's open children, then omit tickets with open blockers or an assignee; the first remaining ticket in map order wins.
- Claim: gh issue edit <n> --add-assignee @me.
- Resolve: comment with the answer, close the issue, then append a context pointer to the map's Decisions-so-far.
