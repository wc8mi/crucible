// SPDX-License-Identifier: MIT
// Copyright (c) 2026 David Behnke
// Crucible's front page news (site/crucible/installer/news.js): More adds the next posts and stops at the last
import { test } from "node:test";
import assert from "node:assert/strict";
import { nextPosts, pressesFor } from "../crucible/installer/news.js";

const feed = (n) => ({ format: "crucible-news/1", first: 5, posts: Array.from({ length: n }, (_, i) => ({ id: `news-${i}`, html: `<article>${i}</article>` })) });

test("More adds the next five, and is done after the last post", () => {
	const f = feed(12);
	let r = nextPosts(f, 5);
	assert.deepEqual(r.posts.map((p) => p.id), ["news-5", "news-6", "news-7", "news-8", "news-9"]);
	assert.equal(r.done, false);
	r = nextPosts(f, 10);
	assert.deepEqual(r.posts.map((p) => p.id), ["news-10", "news-11"]);
	assert.equal(r.done, true, "two left: the button goes");
	assert.equal(nextPosts(feed(10), 5).done, true, "exactly five left: done after one press");
	assert.deepEqual(nextPosts(feed(3), 3), { posts: [], done: true });
	assert.deepEqual(nextPosts({}, 0), { posts: [], done: true }, "a broken feed adds nothing");
});

test("a link to an older post loads enough pages to show it", () => {
	const f = feed(17);
	assert.equal(pressesFor(f, 5, "news-2"), 0, "on the page already");
	assert.equal(pressesFor(f, 5, "news-5"), 1);
	assert.equal(pressesFor(f, 5, "news-9"), 1);
	assert.equal(pressesFor(f, 5, "news-10"), 2);
	assert.equal(pressesFor(f, 5, "news-16"), 3);
	assert.equal(pressesFor(f, 5, "news-nope"), 0, "not in the feed");
});
