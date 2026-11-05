/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   grant.c                                            :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: yourlogin <yourlogin@student.42.fr>        +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/10/30 00:00:00 by yourlogin         #+#    #+#             */
/*   Updated: 2026/10/30 00:00:00 by yourlogin        ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "codexion.h"

static int	can_grant_locked(t_coder *coder, long long now)
{
	t_sim		*sim;
	t_dongle	*a;
	t_dongle	*b;
	int			id;

	sim = coder->sim;
	a = &sim->dongles[coder->left];
	b = &sim->dongles[coder->right];
	id = coder->id - 1;
	return (heap_top(&a->queue) == id && heap_top(&b->queue) == id
		&& a->owner == -1 && b->owner == -1
		&& now >= a->ready_at && now >= b->ready_at);
}

static void	commit_grant_locked(t_coder *coder, long long start)
{
	t_sim	*sim;

	sim = coder->sim;
	heap_pop(sim, &sim->dongles[coder->left].queue);
	heap_pop(sim, &sim->dongles[coder->right].queue);
	sim->dongles[coder->left].owner = coder->id;
	sim->dongles[coder->right].owner = coder->id;
	coder->last_start = start;
}

int	try_grant_locked(t_coder *coder)
{
	t_sim		*sim;
	long long	start;

	sim = coder->sim;
	lock_pair(sim, coder->left, coder->right);
	start = now_ms();
	if (start >= coder->last_start + sim->cfg.burnout
		|| !can_grant_locked(coder, start))
	{
		unlock_pair(sim, coder->left, coder->right);
		return (0);
	}
	commit_grant_locked(coder, start);
	unlock_pair(sim, coder->left, coder->right);
	log_locked(sim, coder->id, "has taken a dongle");
	log_locked(sim, coder->id, "has taken a dongle");
	log_locked(sim, coder->id, "is compiling");
	pthread_cond_broadcast(&sim->changed);
	return (1);
}
