type Ticket = { selection: number; operation: string; sequence: number };

// Selection changes invalidate every pending request, even when returning to A.
// Within one selection, each channel accepts only its latest response.
export function createTeachingScope() {
  let selection = 0;
  let sequence = 0;
  const operations = new Map<string, number>();
  return {
    select(_id: string) {
      selection++;
      operations.clear();
    },
    start(operation: string): Ticket {
      const ticket = { selection, operation, sequence: ++sequence };
      operations.set(operation, ticket.sequence);
      return ticket;
    },
    isCurrent(ticket: Ticket) {
      return (
        ticket.selection === selection &&
        operations.get(ticket.operation) === ticket.sequence
      );
    },
  };
}
