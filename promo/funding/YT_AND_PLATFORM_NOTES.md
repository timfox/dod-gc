# YouTube eligibility + platform notes — researched 2026-11-09 (America/Los_Angeles)

## Official YPP thresholds (verified)
Expanded / fan-funding tier (US eligible):
- 500 subscribers
- 3 valid public uploads in the last 90 days
- AND either 3,000 qualified watch hours (last 12 months) OR 3 million qualified Shorts views (last 90 days)
Source: https://support.google.com/youtube/answer/13429240

Ads / full YPP:
- 1,000 subscribers
- AND either 4,000 qualified watch hours (last 12 months) OR 10 million qualified Shorts views (last 90 days)
Source: https://support.google.com/youtube/answer/72851 and answer/13429240

Note: Shorts watch hours do not count toward the long-form watch-hours threshold.

## Chikato 710 (BulkPublish channel #5974 / UCr-jJObjo7jOUXC6eCUfPJg)
Fetched:
- Subscribers (public About page): **28**
- Lifetime channel views (public About): **2,773**
- Public video count (About): **27 videos**
- BulkPublish get_analytics (YouTube, ~last 30d window returned by API): 2 published posts tracked; 11 scheduled; 1 publishing at fetch time
- BulkPublish post metrics: post 34415 ≈ 1,178 videoViews / 21 likes; post 34416 metrics not yet populated
- Public Shorts tab accessibility labels (approx views, not Studio-qualified): ~5 + 822 + 1.1k + 1k + 73 ≈ **~3k** Shorts views visible on the tab

Cannot fetch from BulkPublish or public pages:
- Qualified Shorts views (last 90 days) as YouTube Studio defines them
- Qualified watch hours (last 12 months)
- Exact count of public uploads in the last 90 days across the whole channel history (BP only tracks posts created through BulkPublish since ~2026-11-07)

## Gap vs thresholds
| Metric | Fan-funding need | Ads need | Current (approx) | Gap |
|---|---|---|---|---|
| Subscribers | 500 | 1,000 | 28 | −472 / −972 |
| Shorts views / 90d | 3,000,000 | 10,000,000 | ~thousands (not Studio-qualified) | millions |
| Watch hours / 12mo | 3,000 | 4,000 | unknown (not available) | unknown |
| Public uploads / 90d | 3 | n/a for ads tier | at least 2 BP-published recently; Shorts tab shows ~5 | likely near/meeting once DoD Shorts land, but sub/view gaps dominate |

Plain answer: **not eligible for YouTube fan funding or ads yet.** Closest path is audience growth; external tips (Ko-fi / GitHub Sponsors) can start immediately.

## Platform recommendation
Start with **Ko-fi (Free, 0% on one-time tips)** for fastest money, then **GitHub Sponsors** for the repo audience (0% on personal sponsorships). Skip Patreon for now (10% platform + processing + payout fees for new creators).

### Ko-fi
- Free to join; Free mode: 0% on one-time tips; 5% on memberships/monthly tips/shop/commissions
- PayPal and/or Stripe; payouts instant to your account
- https://help.ko-fi.com/hc/en-us/articles/360002506494-Does-Ko-fi-take-a-fee
- https://help.ko-fi.com/hc/en-us/articles/115003980093-How-do-I-get-paid

### GitHub Sponsors (individual, US)
- 2FA required; Stripe Connect bank + tax (W-9); Request approval (often a few days)
- 0% fee on sponsorships from personal accounts
- https://docs.github.com/en/sponsors/receiving-sponsorships-through-github-sponsors/setting-up-github-sponsors-for-your-personal-account
- https://docs.github.com/en/sponsors/receiving-sponsorships-through-github-sponsors/about-github-sponsors-for-open-source-contributors

### Patreon (not recommended first)
- New creators after 2025-08-04: 10% platform + ~2.9%+$0.30 processing + payout fees
- https://support.patreon.com/hc/en-us/articles/11111747095181-Creator-fees-overview
- https://www.patreon.com/pricing

## What Tim must do himself to go live
1. Create Ko-fi page, connect PayPal/Stripe, paste KO-FI.md copy, opt out of Standard if wanting 0% tip fees; tell agent the handle.
2. Apply to GitHub Sponsors (2FA, Stripe Connect identity/bank, W-9, tiers, Request approval).
3. After both live: replace TODO links on the site, update SOCIAL_BLURBS, approve commit of FUNDING.yml to xash3d-gc/.github (currently only upstream FWGS donate.md).
4. Deploy site when ready (not done in this job).
5. YouTube Studio → Earn → Get notified for expanded YPP; keep uploading; do not rely on YT money before Wed payday.
