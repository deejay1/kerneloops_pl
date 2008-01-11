#!/bin/sh
git gc
git-push -f ssh://git.infradead.org/srv/git/kerneloops.git master:refs/heads/master
git-push --tags -f ssh://git.infradead.org/srv/git/kerneloops.git master:refs/heads/master
