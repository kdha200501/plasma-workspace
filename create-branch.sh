#!/bin/bash

PACKAGE="plasma-workspace"

# Parse options
while getopts "f:p:h" opt; do
  case $opt in
    f) FEDORA_VERSION="$OPTARG" ;;
    p) PACKAGE_VERSION="$OPTARG" ;;
    h)
      cat <<EOF
Branch off from a upstream tag of $PACKAGE.

Usage: $0 [-f fedora_version] [-p package_version]
  -f  Specify the Fedora release version
  -p  Specify the package version
  -h  Show this help message

Example:
  $0
  $0 -f 43
  $0 -f 43 -p 6.23.0
EOF
      exit 0
      ;;
    *)
      echo "Usage: $0 [-f fedora_version] [-p package_version]" >&2
      exit 1
      ;;
  esac
done

# Set Fedora release version
if [ -z "$FEDORA_VERSION" ]; then
  # Find the host's Fedora version
  HOST_FEDORA_VERSION=$(rpm -E %fedora)

  read -rp "Fedora release version (default: $HOST_FEDORA_VERSION): " FEDORA_VERSION
  FEDORA_VERSION="${FEDORA_VERSION:-$HOST_FEDORA_VERSION}"
fi

# Set package version
if [ -z "$PACKAGE_VERSION" ]; then
  # Find the latest version for the package
  TMP_OUTPUT=$(mktemp)
  printf "\r\e[K🔍 Querying the latest version for package %s" "$PACKAGE"
  dnf --releasever="$FEDORA_VERSION" repoquery --queryformat="%{VERSION}" --latest-limit=1 "$PACKAGE" 2>&1 | tee "$TMP_OUTPUT" | while read -r line; do
    printf "\r\e[K🔍 %s" "${line:0:(($COLUMNS - 3))}"
  done

  if [ "${PIPESTATUS[0]}" -ne 0 ]; then
    printf "\r\e[K❌ Failed to query the latest version for package %s\n" "$PACKAGE"
    rm -f "$TMP_OUTPUT"
    exit 1
  fi

  PACKAGE_LATEST_VERSION=$(tail -n 1 "$TMP_OUTPUT")
  printf "\r\e[K"
  rm -f "$TMP_OUTPUT"

  read -rp "Package version (default: $PACKAGE_LATEST_VERSION): " PACKAGE_VERSION
  PACKAGE_VERSION="${PACKAGE_VERSION:-$PACKAGE_LATEST_VERSION}"
fi

if [ -z "$PACKAGE_VERSION" ]; then
  echo "❌ No package version specified"
  exit 1
fi

# Verify the package version exists for the specific Fedora release
printf "\r\e[K🔍 Validating the package version"
if [ -z "$(dnf --releasever="$FEDORA_VERSION" repoquery --latest-limit=1 "$PACKAGE-$PACKAGE_VERSION*" 2>/dev/null)" ]; then
  printf "\r\e[K❌ %s (%s) is not found in Fedora %s repositories\n" "$PACKAGE" "$PACKAGE_VERSION" "$FEDORA_VERSION"
  exit 1
fi

printf "\r\e[K📦 Package version: %s\n" "$PACKAGE_VERSION"

# Fetch the corresponding tag from upstream
TAG="v$PACKAGE_VERSION"
printf "\r\e[K📥 Fetching tag %s from upstream" "$TAG"
git fetch --no-tags upstream "refs/tags/$TAG:refs/upstream/$TAG" 2>&1 | while read -r line; do
  printf "\r\e[K📥 %s" "${line:0:(($COLUMNS - 3))}"
done

if [ "${PIPESTATUS[0]}" -ne 0 ]; then
  printf "\r\e[K❌ Failed to fetch tag %s\n" "$TAG"
  exit 1
fi

printf "\r\e[K🏷 Upstream tag: %s\n" "$TAG"

# List commits to cherry-pick
BRANCH="customize/$TAG"
read -rp "Show commits to cherry-pick onto $BRANCH? [Y/n] " SHOW_COMMITS
SHOW_COMMITS="${SHOW_COMMITS:-y}"
if [[ "${SHOW_COMMITS,,}" == "y" ]]; then
  TAG_COMMIT=$(git rev-parse refs/upstream/$TAG^{})

  YOUNGEST_BRANCH=""
  YOUNGEST_TAG=""
  YOUNGEST_TIME=0

  while IFS= read -r remote_branch; do
    common_ancestor_commit=$(git merge-base "$remote_branch" "$TAG_COMMIT" 2>/dev/null)
    [ -z "$common_ancestor_commit" ] && continue

    tag=$(git for-each-ref --contains "$common_ancestor_commit" --sort=creatordate --format='%(refname:short)' refs/upstream/ | head -1)
    [ -z "$tag" ] && continue

    tag_time_in_sec=$(git log -1 --format=%at "$tag" 2>/dev/null)
    [ -z "$tag_time_in_sec" ] && continue

    [ "$tag_time_in_sec" -lt "$YOUNGEST_TIME" ] && continue

    YOUNGEST_TIME="$tag_time_in_sec"
    YOUNGEST_BRANCH="$remote_branch"
    YOUNGEST_TAG="$tag"
  done < <(git branch -r | grep 'origin/customize/' | sed 's/^ *//')

  if [ -n "$YOUNGEST_BRANCH" ] && [ -n "$YOUNGEST_TAG" ]; then
    echo "🍒 Commits from $YOUNGEST_BRANCH (based on $YOUNGEST_TAG):"
    git log --oneline --no-decorate "$YOUNGEST_TAG..$YOUNGEST_BRANCH"
  else
    echo "⚠ No existing customize branches found to cherry-pick from"
  fi
fi


# Branch off tag
if git show-ref --verify --quiet "refs/heads/$BRANCH"; then
  printf "\r\e[K🟢 Branch %s already exists\n" "$BRANCH"
  git checkout "$BRANCH" &>/dev/null
  exit 0
fi

git checkout "refs/upstream/$TAG" -b "$BRANCH" &>/dev/null
printf "\r\e[K✨ New branch %s created\n" "$BRANCH"
